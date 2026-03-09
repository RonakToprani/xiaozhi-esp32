#ifndef MOCHI_DISPLAY_H
#define MOCHI_DISPLAY_H

// =============================================================================
// mochi_display.h — Mochi GIF display engine
//
// Manages GIF-based emotion display on the 466x466 round AMOLED.
// GIF frames are decoded and blitted to the LCD panel, centered on screen.
// Runs in a dedicated FreeRTOS task pinned to Core 0 (display core).
//
// Thread-safe: uses a mutex to protect concurrent access to the GIF decoder.
// =============================================================================

#include "mochi_emotion.h"
#include <esp_lcd_panel_ops.h>

// Initialize the Mochi GIF display engine.
// Must be called after the LCD panel is initialized.
// panel: the initialized esp_lcd_panel_handle_t from the board
// screen_w, screen_h: display resolution (466x466)
void mochi_display_init(esp_lcd_panel_handle_t panel, int screen_w, int screen_h);

// Set the current emotion — triggers GIF switch.
// If emotion == current emotion, this is a no-op (no redundant restart).
// Thread-safe: can be called from any task.
void mochi_display_set_emotion(MochiEmotion emotion);

// Get the current emotion being displayed.
MochiEmotion mochi_display_get_emotion();

// Deinitialize the display engine and free resources.
void mochi_display_deinit();

#endif // MOCHI_DISPLAY_H
