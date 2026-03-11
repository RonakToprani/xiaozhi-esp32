#ifndef MOCHI_TOUCH_H
#define MOCHI_TOUCH_H

// =============================================================================
// mochi_touch.h — Mochi touch zone + gesture engine
//
// 5 touch zones (polar coordinates from center of 466×466 display):
//   CENTER:  dist < 80
//   TOP:     dist 80–200, angle 315°–45°   (up)
//   RIGHT:   dist 80–200, angle  45°–135°  (right)
//   BOTTOM:  dist 80–200, angle 135°–225°  (down)
//   LEFT:    dist 80–200, angle 225°–315°  (left)
//   OUTSIDE: dist > 200 — ignored
//
// 7 gestures (software-detected from touch event streams):
//   TAP, LONG_PRESS, DOUBLE_TAP, SWIPE_UP, SWIPE_DOWN, SWIPE_LEFT, SWIPE_RIGHT
//
// FreeRTOS task on Core 0, priority 8. Uses GPIO16 INT for CST816S.
// =============================================================================

#include <driver/i2c_master.h>

// ---------------------------------------------------------------------------
// Touch zones — polar from screen center (233, 233)
// ---------------------------------------------------------------------------
enum class TouchZone {
    kCenter = 0,
    kTop,
    kRight,
    kBottom,
    kLeft,
    kOutside,   // dist > 200 — ignored
};

// ---------------------------------------------------------------------------
// Touch gestures — software-detected
// ---------------------------------------------------------------------------
enum class TouchGesture {
    kTap = 0,
    kLongPress,
    kDoubleTap,
    kSwipeUp,
    kSwipeDown,
    kSwipeLeft,
    kSwipeRight,
    kNone,      // No gesture recognized yet
};

// ---------------------------------------------------------------------------
// Name helpers (for logging)
// ---------------------------------------------------------------------------
inline const char* TouchZoneName(TouchZone z) {
    switch (z) {
        case TouchZone::kCenter:  return "CENTER";
        case TouchZone::kTop:     return "TOP";
        case TouchZone::kRight:   return "RIGHT";
        case TouchZone::kBottom:  return "BOTTOM";
        case TouchZone::kLeft:    return "LEFT";
        case TouchZone::kOutside: return "OUTSIDE";
        default:                  return "UNKNOWN";
    }
}

inline const char* TouchGestureName(TouchGesture g) {
    switch (g) {
        case TouchGesture::kTap:        return "TAP";
        case TouchGesture::kLongPress:  return "LONG_PRESS";
        case TouchGesture::kDoubleTap:  return "DOUBLE_TAP";
        case TouchGesture::kSwipeUp:    return "SWIPE_UP";
        case TouchGesture::kSwipeDown:  return "SWIPE_DOWN";
        case TouchGesture::kSwipeLeft:  return "SWIPE_LEFT";
        case TouchGesture::kSwipeRight: return "SWIPE_RIGHT";
        case TouchGesture::kNone:       return "NONE";
        default:                        return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Initialize the touch engine.
// i2c_bus: the shared I2C master bus handle (bus 0, GPIO47/48)
// Must be called after I2C bus is initialized.
void mochi_touch_init(i2c_master_bus_handle_t i2c_bus);

// Deinitialize the touch engine and free resources.
void mochi_touch_deinit();

#endif // MOCHI_TOUCH_H
