#ifndef MOCHI_PERSONALITY_H
#define MOCHI_PERSONALITY_H

// =============================================================================
// mochi_personality.h — Mochi personality layer
//
// Connects emotion changes to reactive sounds. Fires sounds on emotion
// TRANSITIONS (not continuously). Handles special cases like purr_loop
// (looping on kLoved entry, stopping on any other emotion).
//
// Also manages boredom cascade (future: idle timeout → bored → sleepy)
// and time-of-day greetings (good_morning / good_night).
// =============================================================================

#include "mochi_emotion.h"
#include "mochi_touch.h"

// Initialize the personality layer.
// Must be called after mochi_audio_init() and mochi_display_init().
void mochi_personality_init();

// Called when the displayed emotion changes.
// Triggers the appropriate reactive sound for the transition.
// Must be called from the thread that changes the emotion (typically main task).
void mochi_personality_on_emotion_change(MochiEmotion old_emotion, MochiEmotion new_emotion);

// Called from the touch engine when a zone+gesture combo is detected.
// Triggers instant local reaction: emotion change + sound + subtitle text.
// Thread-safe: can be called from the touch task.
void mochi_touch_trigger_reaction(TouchZone zone, TouchGesture gesture);

// Deinitialize the personality layer.
void mochi_personality_deinit();

#endif // MOCHI_PERSONALITY_H
