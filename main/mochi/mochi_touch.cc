// =============================================================================
// mochi_touch.cc — Mochi touch zone + gesture engine
//
// TOUCH HARDWARE: CST816S (CST820-compatible) on shared I2C bus 0
//   - I2C address: 0x15
//   - INT pin: GPIO16 (active LOW)
//   - Register 0x02: [num_points, x_h:4|0:4, x_l, y_h:4|0:4, y_l]
//   - Coordinate range: 0–465 for 466×466 display (origin top-left)
//   - Single touch only (POINT_NUM_MAX = 1)
//
// ZONE CLASSIFICATION (polar from center 233,233):
//   CENTER:  dist < 80
//   TOP:     dist 80–200, angle 315°–45°   (Note: angle 0° = right, 270° = up)
//   RIGHT:   dist 80–200, angle  45°–135°
//   BOTTOM:  dist 80–200, angle 135°–225°
//   LEFT:    dist 80–200, angle 225°–315°
//   OUTSIDE: dist > 200
//
// GESTURE DETECTION (software state machine):
//   TAP:         press+release <300ms, displacement <10px
//   LONG_PRESS:  press held >800ms, displacement <10px
//   SWIPE_UP:    dy < -60px, |dx| < 40px, duration <500ms
//   SWIPE_DOWN:  dy > 60px, |dx| < 40px, duration <500ms
//   SWIPE_LEFT:  dx < -60px, |dy| < 40px, duration <500ms
//   SWIPE_RIGHT: dx > 60px, |dy| < 40px, duration <500ms
//   DOUBLE_TAP:  two taps within 400ms in same zone
//
// CALIBRATION DATA (Session 4):
//   (to be filled after hardware testing)
// =============================================================================

#include "mochi_touch.h"
#include "mochi_personality.h"
#include "mochi_display.h"
#include "mochi_audio.h"
#include "application.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <cmath>
#include <cstring>

static const char* TAG = "MOCHI_TOUCH";

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr uint8_t  CST816S_ADDR        = 0x15;
static constexpr uint8_t  CST816S_DATA_REG    = 0x02;
static constexpr gpio_num_t TOUCH_INT_PIN      = GPIO_NUM_16;
static constexpr int      SCREEN_CENTER        = 233;   // 466/2
static constexpr int      ZONE_INNER_R         = 80;    // Center zone radius
static constexpr int      ZONE_OUTER_R         = 200;   // Max touchable radius
static constexpr int      TAP_MAX_MS           = 300;
static constexpr int      TAP_MAX_DISP         = 15;    // pixels (relaxed for small screen)
static constexpr int      LONG_PRESS_MS        = 800;
static constexpr int      SWIPE_MIN_PX         = 35;    // Lowered from 60 — 1.32" screen is very small
static constexpr int      SWIPE_MAX_CROSS_PX   = 80;    // Relaxed from 40 — small round screen needs tolerance
static constexpr int      SWIPE_MAX_MS         = 600;   // Slightly more time allowed for swipe
static constexpr int      DOUBLE_TAP_MS        = 400;
static constexpr int      DEBOUNCE_MS          = 50;
static constexpr int      POLL_INTERVAL_MS     = 15;    // ~66Hz polling when touch active

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool                     s_initialized = false;
static i2c_master_dev_handle_t  s_touch_dev   = NULL;
static TaskHandle_t             s_touch_task   = NULL;
static SemaphoreHandle_t        s_touch_sem    = NULL;  // Binary semaphore from ISR

// Touch state machine
struct TouchState {
    bool     pressed;
    int      start_x, start_y;   // Touch-down coordinates
    int      cur_x, cur_y;       // Current/last coordinates
    int64_t  press_time;         // Timestamp of touch-down (us)
    bool     long_press_fired;   // Already fired long press for this touch
};
static TouchState s_state = {};

// Double-tap tracking
static int64_t  s_last_tap_time = 0;
static TouchZone s_last_tap_zone = TouchZone::kOutside;

// Debounce
static int64_t s_last_event_time = 0;

// ---------------------------------------------------------------------------
// I2C read helper
// ---------------------------------------------------------------------------
static bool touch_i2c_read(uint8_t reg, uint8_t* data, size_t len) {
    esp_err_t ret = i2c_master_transmit_receive(s_touch_dev, &reg, 1, data, len, 100);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C read reg 0x%02x failed: %s", reg, esp_err_to_name(ret));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Read raw touch data. Returns true if touch is active.
// ---------------------------------------------------------------------------
static bool touch_read_raw(int* x, int* y) {
    uint8_t buf[5] = {0};
    if (!touch_i2c_read(CST816S_DATA_REG, buf, 5)) {
        return false;
    }

    uint8_t num_points = buf[0];
    if (num_points == 0) {
        return false;  // No touch
    }

    // Extract 12-bit coordinates
    *x = (((uint16_t)(buf[1] & 0x0F)) << 8) | buf[2];
    *y = (((uint16_t)(buf[3] & 0x0F)) << 8) | buf[4];

    // Clamp to display bounds
    if (*x >= 466) *x = 465;
    if (*y >= 466) *y = 465;

    // Display is rotated 180° (SetMIRROR_XY 0xC0) — rotate touch to match
    *x = 465 - *x;
    *y = 465 - *y;

    return true;
}

// ---------------------------------------------------------------------------
// Zone classification using polar coordinates from screen center
// ---------------------------------------------------------------------------
static TouchZone classify_zone(int x, int y) {
    int dx = x - SCREEN_CENTER;
    int dy = y - SCREEN_CENTER;
    float dist = sqrtf((float)(dx * dx + dy * dy));

    if (dist < ZONE_INNER_R) {
        return TouchZone::kCenter;
    }
    if (dist > ZONE_OUTER_R) {
        return TouchZone::kOutside;
    }

    // atan2 returns radians: 0=right, pi/2=down, -pi/2=up (screen coords: y increases down)
    float angle_rad = atan2f((float)dy, (float)dx);
    float angle_deg = angle_rad * 180.0f / M_PI;
    if (angle_deg < 0) angle_deg += 360.0f;

    // Map angles to zones:
    // Screen: y=0 is top, y=466 is bottom
    // atan2 with screen coords: 0°=right, 90°=down, 180°=left, 270°=up
    // TOP:    270°–360° and 0°–90° → wait, let me think about this with screen coords
    //
    // In screen coordinates (y-down):
    //   angle 0°   = right  (+x, 0)
    //   angle 90°  = down   (0, +y)
    //   angle 180° = left   (-x, 0)
    //   angle 270° = up     (0, -y)
    //
    // Session spec says:
    //   TOP:    315°–45°  → but in screen coords, "up" is 270°
    //   BOTTOM: 135°–225° → and "down" is 90°
    //
    // Correcting for screen coords (y-inverted vs math coords):
    //   TOP (screen up):    angle 270°–315° and 225°–270° → simplify: 225°–315°
    //   Hmm, let me re-read the spec. The spec says angle definitions:
    //   TOP: 315°–45° — this uses MATH convention (y-up, 0°=right, 90°=up)
    //
    // So convert: screen_angle to math_angle: math_angle = 360° - screen_angle
    // OR equivalently, just negate dy before computing atan2.
    float math_angle_rad = atan2f((float)(-dy), (float)dx);  // Negate dy for math coords
    float math_angle = math_angle_rad * 180.0f / M_PI;
    if (math_angle < 0) math_angle += 360.0f;

    // Now in math convention: 0°=right, 90°=up, 180°=left, 270°=down
    // TOP:    315°–360° or 0°–45°
    // RIGHT:  45°–135°
    // BOTTOM: 135°–225° (actually "down" in math = 270°, hmm)
    //
    // Wait — the session spec defines zones as:
    //   TOP:     angle 315°–45°   → this is "up" in standard math (around 0°/360°, right? No.)
    //
    // Let me re-read: "TOP: dist 80-200, angle 315°-45°"
    // In standard polar (math convention): 0°=right, 90°=up
    // 315°–45° wraps around 0° (the right side), NOT the top.
    //
    // This doesn't make sense for "TOP" unless the convention is:
    //   0° = up, 90° = right (compass-like)
    //
    // That's compass convention! Let me use that:
    //   0°/360° = up (north)
    //   90°  = right (east)
    //   180° = down (south)
    //   270° = left (west)
    //
    // So: compass_angle = atan2(dx, -dy) in screen coords
    // (dx=east component, -dy=north component since y is inverted)
    float compass_rad = atan2f((float)dx, (float)(-dy));
    float compass_deg = compass_rad * 180.0f / M_PI;
    if (compass_deg < 0) compass_deg += 360.0f;

    // Now compass_deg: 0°=up, 90°=right, 180°=down, 270°=left
    // TOP:    315°–45°  (around 0°/360° = up) ✓
    // RIGHT:  45°–135°  (around 90° = right) ✓
    // BOTTOM: 135°–225° (around 180° = down) ✓
    // LEFT:   225°–315° (around 270° = left) ✓

    if (compass_deg >= 315.0f || compass_deg < 45.0f) {
        return TouchZone::kTop;
    } else if (compass_deg >= 45.0f && compass_deg < 135.0f) {
        return TouchZone::kRight;
    } else if (compass_deg >= 135.0f && compass_deg < 225.0f) {
        return TouchZone::kBottom;
    } else {
        return TouchZone::kLeft;
    }
}

// ---------------------------------------------------------------------------
// Forward declaration
// ---------------------------------------------------------------------------
extern void mochi_touch_trigger_reaction(TouchZone zone, TouchGesture gesture);

// ---------------------------------------------------------------------------
// Dispatch gesture event (debounced)
// ---------------------------------------------------------------------------
static void dispatch_gesture(TouchZone zone, TouchGesture gesture) {
    int64_t now = esp_timer_get_time();
    if ((now - s_last_event_time) < (DEBOUNCE_MS * 1000)) {
        ESP_LOGD(TAG, "Debounced gesture %s", TouchGestureName(gesture));
        return;
    }
    s_last_event_time = now;

    if (zone == TouchZone::kOutside) {
        ESP_LOGD(TAG, "Touch outside visible circle — ignored");
        return;
    }

    ESP_LOGI(TAG, "TOUCH: zone=%s gesture=%s", TouchZoneName(zone), TouchGestureName(gesture));
    mochi_touch_trigger_reaction(zone, gesture);
}

// ---------------------------------------------------------------------------
// Process touch-down event
// ---------------------------------------------------------------------------
static void on_touch_down(int x, int y) {
    s_state.pressed = true;
    s_state.start_x = x;
    s_state.start_y = y;
    s_state.cur_x = x;
    s_state.cur_y = y;
    s_state.press_time = esp_timer_get_time();
    s_state.long_press_fired = false;

    ESP_LOGD(TAG, "DOWN: x=%d y=%d", x, y);
}

// ---------------------------------------------------------------------------
// Process touch-move event
// ---------------------------------------------------------------------------
static void on_touch_move(int x, int y) {
    s_state.cur_x = x;
    s_state.cur_y = y;
}

// ---------------------------------------------------------------------------
// Process touch-up event — classify gesture
// ---------------------------------------------------------------------------
static void on_touch_up() {
    if (!s_state.pressed) return;
    s_state.pressed = false;

    int64_t now = esp_timer_get_time();
    int64_t duration_us = now - s_state.press_time;
    int duration_ms = (int)(duration_us / 1000);

    int dx = s_state.cur_x - s_state.start_x;
    int dy = s_state.cur_y - s_state.start_y;
    int abs_dx = abs(dx);
    int abs_dy = abs(dy);
    int displacement = (int)sqrtf((float)(dx * dx + dy * dy));

    TouchZone zone = classify_zone(s_state.start_x, s_state.start_y);

    ESP_LOGD(TAG, "UP: dx=%d dy=%d disp=%d dur=%dms zone=%s",
             dx, dy, displacement, duration_ms, TouchZoneName(zone));

    // Already handled as long press?
    if (s_state.long_press_fired) {
        ESP_LOGD(TAG, "Long press already fired — ignoring release");
        return;
    }

    // Swipe detection (must be fast and have enough displacement in one axis)
    if (duration_ms < SWIPE_MAX_MS) {
        // Swipe UP: dy negative (finger moved toward top of screen)
        if (dy < -SWIPE_MIN_PX && abs_dx < SWIPE_MAX_CROSS_PX) {
            dispatch_gesture(zone, TouchGesture::kSwipeUp);
            return;
        }
        // Swipe DOWN: dy positive (finger moved toward bottom)
        if (dy > SWIPE_MIN_PX && abs_dx < SWIPE_MAX_CROSS_PX) {
            dispatch_gesture(zone, TouchGesture::kSwipeDown);
            return;
        }
        // Swipe LEFT: dx negative
        if (dx < -SWIPE_MIN_PX && abs_dy < SWIPE_MAX_CROSS_PX) {
            dispatch_gesture(zone, TouchGesture::kSwipeLeft);
            return;
        }
        // Swipe RIGHT: dx positive
        if (dx > SWIPE_MIN_PX && abs_dy < SWIPE_MAX_CROSS_PX) {
            dispatch_gesture(zone, TouchGesture::kSwipeRight);
            return;
        }
    }

    // Tap detection
    if (duration_ms < TAP_MAX_MS && displacement < TAP_MAX_DISP) {
        // Check for double tap
        if ((now - s_last_tap_time) < (DOUBLE_TAP_MS * 1000) &&
            s_last_tap_zone == zone) {
            s_last_tap_time = 0;  // Reset so triple-tap doesn't fire
            s_last_tap_zone = TouchZone::kOutside;
            dispatch_gesture(zone, TouchGesture::kDoubleTap);
            return;
        }
        // Single tap — record for potential double-tap
        s_last_tap_time = now;
        s_last_tap_zone = zone;
        dispatch_gesture(zone, TouchGesture::kTap);
        return;
    }

    // If none matched (slow drag, etc.) — ignore
    ESP_LOGD(TAG, "Unrecognized gesture — ignored");
}

// ---------------------------------------------------------------------------
// Check for long press while touch is held
// ---------------------------------------------------------------------------
static void check_long_press() {
    if (!s_state.pressed || s_state.long_press_fired) return;

    int64_t now = esp_timer_get_time();
    int duration_ms = (int)((now - s_state.press_time) / 1000);

    if (duration_ms < LONG_PRESS_MS) return;

    int dx = s_state.cur_x - s_state.start_x;
    int dy = s_state.cur_y - s_state.start_y;
    int displacement = (int)sqrtf((float)(dx * dx + dy * dy));

    if (displacement < TAP_MAX_DISP) {
        s_state.long_press_fired = true;
        TouchZone zone = classify_zone(s_state.start_x, s_state.start_y);
        dispatch_gesture(zone, TouchGesture::kLongPress);
    }
}

// ---------------------------------------------------------------------------
// GPIO ISR — gives semaphore to wake touch task
// ---------------------------------------------------------------------------
static void IRAM_ATTR touch_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_touch_sem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ---------------------------------------------------------------------------
// Touch task — runs on Core 0, priority 8
// ---------------------------------------------------------------------------
static void touch_task(void* arg) {
    ESP_LOGI(TAG, "Touch task started on core %d", xPortGetCoreID());

    bool was_pressed = false;
    int x, y;

    while (true) {
        // Wait for touch interrupt or timeout (for long-press detection and polling)
        xSemaphoreTake(s_touch_sem, pdMS_TO_TICKS(POLL_INTERVAL_MS));

        bool is_pressed = touch_read_raw(&x, &y);

        if (is_pressed && !was_pressed) {
            // Touch down
            on_touch_down(x, y);
        } else if (is_pressed && was_pressed) {
            // Touch move
            on_touch_move(x, y);
        } else if (!is_pressed && was_pressed) {
            // Touch up
            on_touch_up();
        }

        // Check for long press while held
        if (is_pressed) {
            check_long_press();
        }

        was_pressed = is_pressed;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void mochi_touch_init(i2c_master_bus_handle_t i2c_bus) {
    if (s_initialized) {
        ESP_LOGW(TAG, "Touch engine already initialized");
        return;
    }

    // Add CST816S as I2C device on the shared bus
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = CST816S_ADDR;
    dev_cfg.scl_speed_hz = 100000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_touch_dev));
    ESP_LOGI(TAG, "CST816S added to I2C bus at 0x%02x", CST816S_ADDR);

    // Verify chip is present by reading chip ID
    uint8_t chip_id = 0;
    if (touch_i2c_read(0xA7, &chip_id, 1)) {
        ESP_LOGI(TAG, "CST816S chip ID: %d", chip_id);
    } else {
        ESP_LOGW(TAG, "CST816S chip ID read failed — touch may not work");
    }

    // Create binary semaphore for ISR → task notification
    s_touch_sem = xSemaphoreCreateBinary();
    assert(s_touch_sem != NULL);

    // Configure GPIO16 as interrupt input (active LOW)
    gpio_config_t int_cfg = {};
    int_cfg.pin_bit_mask = BIT64(TOUCH_INT_PIN);
    int_cfg.mode = GPIO_MODE_INPUT;
    int_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    int_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    int_cfg.intr_type = GPIO_INTR_NEGEDGE;
    ESP_ERROR_CHECK(gpio_config(&int_cfg));

    // Install GPIO ISR service and attach handler
    gpio_install_isr_service(0);
    ESP_ERROR_CHECK(gpio_isr_handler_add(TOUCH_INT_PIN, touch_isr_handler, NULL));
    ESP_LOGI(TAG, "Touch INT configured on GPIO%d", TOUCH_INT_PIN);

    // Clear initial state
    memset(&s_state, 0, sizeof(s_state));
    s_last_tap_time = 0;
    s_last_tap_zone = TouchZone::kOutside;
    s_last_event_time = 0;

    // Create touch task on Core 0
    xTaskCreatePinnedToCore(
        touch_task,
        "mochi_touch",
        4096,        // 4KB stack — sufficient for math + I2C
        NULL,
        8,           // Priority 8 — higher than display (5), lower than audio (10)
        &s_touch_task,
        0            // Core 0 (display core)
    );

    s_initialized = true;
    ESP_LOGI(TAG, "Touch engine initialized — 5 zones, 7 gestures");
}

void mochi_touch_deinit() {
    if (!s_initialized) return;

    if (s_touch_task) {
        vTaskDelete(s_touch_task);
        s_touch_task = NULL;
    }

    gpio_isr_handler_remove(TOUCH_INT_PIN);

    if (s_touch_sem) {
        vSemaphoreDelete(s_touch_sem);
        s_touch_sem = NULL;
    }

    if (s_touch_dev) {
        i2c_master_bus_rm_device(s_touch_dev);
        s_touch_dev = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "Touch engine deinitialized");
}
