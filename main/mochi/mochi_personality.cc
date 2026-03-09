// =============================================================================
// mochi_personality.cc — Mochi personality layer
//
// EMOTION → SOUND MAPPING (Session 3, Step 4):
//
//   kHappy     → chirp_happy (on GIF switch event)
//   kExcited   → chirp_excited OR joy_burst (random pick)
//   kLoved     → purr_loop (loops until emotion changes)
//   kSad       → sigh_relieved (ironic sad sound)
//   kAngry     → grunt_angry
//   kScared    → yelp_scared
//   kStartled  → yelp_scared (same clip, shorter duration)
//   kRelieved  → sigh_relieved
//   kSleepy    → yawn
//   kBored     → lonely_melody
//   kListening → confirm (soft click, 200ms max)
//   kIdle      → (silence, or purr_loop at 20% vol after 2min idle — future)
//
// Sound fires on emotion CHANGE (not continuously).
// Exception: purr_loop fires on kLoved entry, stops on any other emotion.
// =============================================================================

#include "mochi_personality.h"
#include "mochi_audio.h"

#include <esp_log.h>
#include <esp_random.h>

static const char* TAG = "MOCHI_PERS";

static bool s_initialized = false;

// Track if purr_loop is active so we can stop it on transition out
static bool s_purring = false;

void mochi_personality_init() {
    if (s_initialized) {
        ESP_LOGW(TAG, "Personality layer already initialized");
        return;
    }

    s_initialized = true;
    s_purring = false;
    ESP_LOGI(TAG, "Personality layer initialized");
}

void mochi_personality_on_emotion_change(MochiEmotion old_emotion, MochiEmotion new_emotion) {
    if (!s_initialized) return;
    if (old_emotion == new_emotion) return;  // No change, no sound

    ESP_LOGI(TAG, "EMOTION_CHANGE: %s -> %s",
             MochiEmotionName(old_emotion), MochiEmotionName(new_emotion));

    // Stop purr_loop if transitioning away from kLoved
    if (s_purring && new_emotion != MochiEmotion::kLoved) {
        mochi_audio_stop();
        s_purring = false;
        ESP_LOGI(TAG, "Purr stopped (emotion changed from loved)");
    }

    // Fire sound for the new emotion
    switch (new_emotion) {
        case MochiEmotion::kHappy:
            mochi_audio_play(SoundClip::kChirpHappy);
            break;

        case MochiEmotion::kExcited: {
            // Random pick between chirp_excited and joy_burst
            if (esp_random() % 2 == 0) {
                mochi_audio_play(SoundClip::kChirpExcited);
            } else {
                mochi_audio_play(SoundClip::kJoyBurst);
            }
            break;
        }

        case MochiEmotion::kLoved:
            // Start looping purr
            mochi_audio_play_looping(SoundClip::kPurrLoop);
            s_purring = true;
            break;

        case MochiEmotion::kSad:
            mochi_audio_play(SoundClip::kSighRelieved);
            break;

        case MochiEmotion::kAngry:
            mochi_audio_play(SoundClip::kGruntAngry);
            break;

        case MochiEmotion::kScared:
            mochi_audio_play(SoundClip::kYelpScared);
            break;

        case MochiEmotion::kStartled:
            mochi_audio_play(SoundClip::kYelpScared);  // Same clip as scared
            break;

        case MochiEmotion::kRelieved:
            mochi_audio_play(SoundClip::kSighRelieved);
            break;

        case MochiEmotion::kSleepy:
            mochi_audio_play(SoundClip::kYawn);
            break;

        case MochiEmotion::kBored:
            mochi_audio_play(SoundClip::kLonelyMelody);
            break;

        case MochiEmotion::kListening:
            mochi_audio_play(SoundClip::kConfirm);
            break;

        case MochiEmotion::kIdle:
            // Silence on idle transition (future: purr at 20% after 2min)
            break;

        case MochiEmotion::kThinking:
        case MochiEmotion::kTalking:
        case MochiEmotion::kConfused:
            // No sound for these transitions
            break;

        default:
            break;
    }
}

void mochi_personality_deinit() {
    if (!s_initialized) return;

    if (s_purring) {
        mochi_audio_stop();
        s_purring = false;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "Personality layer deinitialized");
}
