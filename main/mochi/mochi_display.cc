// =============================================================================
// mochi_display.cc — Mochi GIF display engine
//
// Manages emotion→GIF mapping and display on the 466x466 round AMOLED.
// GIF frames are decoded and centered on the round display.
//
// Architecture:
//   - A FreeRTOS task (pinned to Core 0) runs the GIF decode/render loop
//   - A mutex protects concurrent access to the GIF decoder state
//   - mochi_display_set_emotion() is thread-safe and can be called from any task
//   - Redundant emotion sets are skipped (no GIF restart if same emotion)
//
// Dependency graph:
//   application.cc OnIncomingJson (type=="llm")
//     → EmotionFromString(emotion_str) / EmotionFromEmoji(text)
//     → mochi_display_set_emotion(emotion)
//       → stops current GIF
//       → resolves SPIFFS path: /spiffs/gifs/<emotion_name>.gif
//       → loads & starts new GIF
//       → logs: MOCHI: GIF_LOAD: <emotion>.gif
//
//   application.cc HandleStateChangedEvent
//     → kDeviceStateIdle      → kIdle (if no recent LLM emotion)
//     → kDeviceStateListening → kListening (always)
//     → kDeviceStateConnecting→ kThinking (always)
//     → kDeviceStateSpeaking  → NO CHANGE (LLM emotion holds)
// =============================================================================

#include "mochi_display.h"
#include "mochi_personality.h"

#include <cstring>
#include <cstdio>
#include <sys/stat.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "MOCHI";

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static MochiEmotion       s_current_emotion  = MochiEmotion::kIdle;
static MochiEmotion       s_pending_emotion  = MochiEmotion::kIdle;
static volatile bool      s_emotion_changed  = false;
static SemaphoreHandle_t  s_mutex            = nullptr;
static TaskHandle_t       s_task_handle      = nullptr;
static esp_lcd_panel_handle_t s_panel        = nullptr;
static int                s_screen_w         = 466;
static int                s_screen_h         = 466;
static bool               s_initialized      = false;

// GIF dimensions (all Mochi GIFs are 150x150)
static const int GIF_W = 150;
static const int GIF_H = 150;

// ---------------------------------------------------------------------------
// Helper: check if a GIF file exists on SPIFFS
// ---------------------------------------------------------------------------
static bool gif_file_exists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

// ---------------------------------------------------------------------------
// GIF render task — pinned to Core 0 (display core)
//
// Waits for emotion changes, then loads and plays the corresponding GIF.
// Currently a stub: logs the GIF path and waits for the next change.
// Full GIF decode + panel blit will be implemented when GIF assets are ready.
// ---------------------------------------------------------------------------
static void mochi_display_task(void* arg) {
    char gif_path[64];

    ESP_LOGI(TAG, "GIF display task started on Core %d", xPortGetCoreID());

    for (;;) {
        // Wait for an emotion change notification
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        if (!s_emotion_changed) {
            continue;
        }

        // Grab the pending emotion under mutex
        MochiEmotion new_emotion;
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            new_emotion = s_pending_emotion;
            s_emotion_changed = false;
            xSemaphoreGive(s_mutex);
        } else {
            ESP_LOGW(TAG, "GIF task: mutex timeout");
            continue;
        }

        // Skip if same emotion (redundancy guard)
        if (new_emotion == s_current_emotion && s_current_emotion != MochiEmotion::kIdle) {
            continue;
        }

        MochiEmotion old_emotion = s_current_emotion;
        s_current_emotion = new_emotion;

        // Fire reactive sound for this emotion transition
        mochi_personality_on_emotion_change(old_emotion, new_emotion);

        // Resolve GIF path
        MochiEmotionGifPath(new_emotion, gif_path, sizeof(gif_path));

        int64_t t_start = esp_timer_get_time();
        ESP_LOGI(TAG, "GIF_LOAD: %s.gif (t=%" PRId64 "us)", MochiEmotionName(new_emotion), t_start);

        // Check if the GIF file exists on SPIFFS
        if (!gif_file_exists(gif_path)) {
            ESP_LOGW(TAG, "GIF_MISSING: %s — falling back to idle", gif_path);
            // Fall back to idle if the specific GIF is missing
            if (new_emotion != MochiEmotion::kIdle) {
                MochiEmotionGifPath(MochiEmotion::kIdle, gif_path, sizeof(gif_path));
                if (!gif_file_exists(gif_path)) {
                    ESP_LOGW(TAG, "GIF_MISSING: idle.gif — no GIFs available yet");
                    continue;
                }
            } else {
                continue;
            }
        }

        // ---------------------------------------------------------------
        // TODO: Full GIF decode + render loop
        //
        // When GIF assets are placed on SPIFFS, this section will:
        // 1. Open GIF file with gd_open_gif_file() or fopen()
        // 2. Decode each frame to an RGB565 buffer
        // 3. Center the 150x150 frame on the 466x466 display:
        //    x_offset = (466 - 150) / 2 = 158
        //    y_offset = (466 - 150) / 2 = 158
        // 4. Call esp_lcd_panel_draw_bitmap(s_panel, x, y, x+w, y+h, buf)
        // 5. Delay by frame duration (from GIF header)
        // 6. Loop until emotion changes or GIF loops
        //
        // The panel handle (s_panel) and screen dimensions are already
        // stored from mochi_display_init().
        // ---------------------------------------------------------------

        ESP_LOGI(TAG, "GIF_READY: %s (would render at offset %d,%d on %dx%d)",
                 MochiEmotionName(new_emotion),
                 (s_screen_w - GIF_W) / 2, (s_screen_h - GIF_H) / 2,
                 s_screen_w, s_screen_h);

        // Log heap status after each GIF switch for memory leak detection
        ESP_LOGI(TAG, "HEAP: free=%lu, min=%lu",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
    }
}

// =============================================================================
// Public API
// =============================================================================

void mochi_display_init(esp_lcd_panel_handle_t panel, int screen_w, int screen_h) {
    if (s_initialized) {
        ESP_LOGW(TAG, "mochi_display already initialized");
        return;
    }

    s_panel    = panel;
    s_screen_w = screen_w;
    s_screen_h = screen_h;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create GIF mutex");
        return;
    }

    // Create GIF render task pinned to Core 0 (display core)
    BaseType_t ret = xTaskCreatePinnedToCore(
        mochi_display_task,
        "mochi_gif",
        4096,           // Stack size (sufficient for path operations + decode)
        nullptr,
        5,              // Priority (same as emote task)
        &s_task_handle,
        0               // Core 0 — display core
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GIF display task");
        vSemaphoreDelete(s_mutex);
        s_mutex = nullptr;
        return;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Display engine initialized (%dx%d, GIF %dx%d centered at %d,%d)",
             screen_w, screen_h, GIF_W, GIF_H,
             (screen_w - GIF_W) / 2, (screen_h - GIF_H) / 2);
}

void mochi_display_set_emotion(MochiEmotion emotion) {
    if (!s_initialized) {
        ESP_LOGW(TAG, "Display not initialized, ignoring emotion set");
        return;
    }

    // Skip redundant sets
    if (emotion == s_current_emotion && !s_emotion_changed) {
        return;
    }

    // Set pending emotion under mutex
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_pending_emotion = emotion;
        s_emotion_changed = true;
        xSemaphoreGive(s_mutex);

        // Wake the GIF task
        if (s_task_handle) {
            xTaskNotifyGive(s_task_handle);
        }
    } else {
        ESP_LOGW(TAG, "set_emotion: mutex timeout");
    }
}

MochiEmotion mochi_display_get_emotion() {
    return s_current_emotion;
}

void mochi_display_deinit() {
    if (!s_initialized) return;

    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = nullptr;
    }

    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = nullptr;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "Display engine deinitialized");
}
