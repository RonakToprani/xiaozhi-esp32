#ifndef MOCHI_AUDIO_H
#define MOCHI_AUDIO_H

// =============================================================================
// mochi_audio.h — Mochi reactive sound engine
//
// AUDIO PIPELINE AUDIT (Session 3, Step 1):
//   - ES8311 codec on I2S_NUM_0, 24kHz, 16-bit, mono (I2S slots are stereo)
//   - DMA: 6 descriptors x 240 frame samples
//   - PA pin: GPIO46, active HIGH
//   - I2C: SDA=GPIO47, SCL=GPIO48
//   - Xiaozhi AudioOutputTask writes decoded TTS (Opus→PCM) to codec via
//     playback queue. Output auto-enabled on first write, auto-disabled
//     after 15s idle by audio_power_timer_.
//   - data_if_mutex_ protects Enable/Disable only, NOT held during streaming.
//
// I2S SHARING STRATEGY (Step 3):
//   ESP32-S3 has 2 I2S peripherals, but ES8311 is hardwired to I2S_NUM_0.
//   Cannot use I2S_NUM_1 — no physical DAC connection.
//
//   Solution: Direct codec write from dedicated Mochi audio task.
//   - Get AudioCodec* from Board::GetInstance().GetAudioCodec()
//   - Before EVERY write: check is_mochi_audio_allowed()
//     Returns false if kDeviceStateSpeaking or kDeviceStateListening
//   - When idle, Xiaozhi's AudioOutputTask is blocked on empty queue,
//     so our writes are the ONLY I2S writes — no conflict.
//   - PCM files are 16kHz; codec runs at 24kHz → simple linear resampling
//     (upsample 2:3 ratio) done in our task before writing.
//   - On state change to speaking/listening: immediate stop, release codec.
//
// PCM FORMAT:
//   16kHz, mono, 16-bit signed little-endian
//   SPIFFS path: /spiffs/sounds/<name>.pcm
// =============================================================================

#include <cstdint>
#include <cstdio>

// ---------------------------------------------------------------------------
// 14 Mochi sound clips — each maps to a PCM file on SPIFFS
// ---------------------------------------------------------------------------
enum class SoundClip {
    kChirpHappy = 0,
    kChirpExcited,
    kPurrLoop,
    kYawn,
    kYelpScared,
    kSighRelieved,
    kGruntAngry,
    kAttentionMelody,
    kLonelyMelody,
    kGoodMorning,
    kGoodNight,
    kDizzy,
    kJoyBurst,
    kConfirm,

    kCount  // Sentinel — must be last
};

// ---------------------------------------------------------------------------
// Sound clip name (matches PCM filename without extension)
// ---------------------------------------------------------------------------
inline const char* SoundClipName(SoundClip clip) {
    switch (clip) {
        case SoundClip::kChirpHappy:      return "chirp_happy";
        case SoundClip::kChirpExcited:    return "chirp_excited";
        case SoundClip::kPurrLoop:        return "purr_loop";
        case SoundClip::kYawn:            return "yawn";
        case SoundClip::kYelpScared:      return "yelp_scared";
        case SoundClip::kSighRelieved:    return "sigh_relieved";
        case SoundClip::kGruntAngry:      return "grunt_angry";
        case SoundClip::kAttentionMelody: return "attention_melody";
        case SoundClip::kLonelyMelody:    return "lonely_melody";
        case SoundClip::kGoodMorning:     return "good_morning";
        case SoundClip::kGoodNight:       return "good_night";
        case SoundClip::kDizzy:           return "dizzy";
        case SoundClip::kJoyBurst:        return "joy_burst";
        case SoundClip::kConfirm:         return "confirm";
        default:                          return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Asset name helper: sounds/<name>.pcm (used for mmap_assets lookup)
// ---------------------------------------------------------------------------
inline const char* SoundClipAssetName(SoundClip clip, char* buf, size_t buf_size) {
    snprintf(buf, buf_size, "sounds/%s.pcm", SoundClipName(clip));
    return buf;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Initialize the Mochi sound engine.
// Must be called after Board and AudioCodec are initialized.
void mochi_audio_init();

// Play a sound clip once. Stops any currently playing sound first.
// No-op if device state does not allow playback (speaking/listening).
void mochi_audio_play(SoundClip clip);

// Play a sound clip in a loop until explicitly stopped or state changes.
// Used for purr_loop.
void mochi_audio_play_looping(SoundClip clip);

// Stop any currently playing sound immediately.
void mochi_audio_stop();

// Check if a sound is currently playing.
bool mochi_audio_is_playing();

// Set reactive sound volume (0-100). Default: 60.
void mochi_audio_set_volume(uint8_t volume);

// Get current volume.
uint8_t mochi_audio_get_volume();

// Check if Mochi audio is allowed to play right now.
// Returns false during kDeviceStateSpeaking, kDeviceStateListening.
bool is_mochi_audio_allowed();

// Deinitialize the sound engine and free resources.
void mochi_audio_deinit();

#endif // MOCHI_AUDIO_H
