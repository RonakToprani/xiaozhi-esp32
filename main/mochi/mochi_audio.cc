// =============================================================================
// mochi_audio.cc — Mochi reactive sound engine
//
// Plays 16kHz mono 16-bit PCM from memory-mapped assets through ES8311 codec.
// Shares I2S_NUM_0 with Xiaozhi TTS by state-gating: only plays during
// kDeviceStateIdle. Immediately stops on state change to speaking/listening.
//
// Architecture:
//   - Dedicated FreeRTOS task pinned to Core 1, priority 5
//   - Task notification wakes the task for play/stop commands
//   - PCM data read from mmap'd assets partition (zero-copy lookup)
//   - Resamples 16kHz → 24kHz (3:2 upsampling) before codec write
//   - Checks is_mochi_audio_allowed() before EVERY write buffer
//   - Volume control via sample scaling (0-100 → 0.0-1.0)
//
// I2S SHARING STRATEGY:
//   We write directly through Board::GetInstance().GetAudioCodec() when
//   Xiaozhi's AudioOutputTask is blocked on its empty playback queue (idle).
//   State gating ensures mutual exclusion without explicit mutexes on I2S.
//   Before each write, we enable output (if not already) and after playback
//   finishes, we let Xiaozhi's audio_power_timer_ handle disabling.
// =============================================================================

#include "mochi_audio.h"

#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "board.h"
#include "audio_codec.h"
#include "application.h"
#include "assets.h"

static const char* TAG = "MOCHI_AUDIO";

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static const int PCM_SAMPLE_RATE       = 16000;   // Our PCM files
static const int CODEC_SAMPLE_RATE     = 24000;   // ES8311 output rate
static const int READ_BUF_SAMPLES      = 1024;    // Samples per read chunk
static const int WRITE_BUF_SAMPLES     = 1536;    // After 16k→24k resample (1024 * 3/2)
static const int AUDIO_TASK_STACK_SIZE = 6144;     // Need headroom: vector alloc + codec call
static const int AUDIO_TASK_PRIORITY   = 5;
static const int AUDIO_TASK_CORE       = 1;

// Task notification bits
static const uint32_t NOTIFY_PLAY = (1 << 0);
static const uint32_t NOTIFY_STOP = (1 << 1);

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static TaskHandle_t    s_task_handle   = nullptr;
static SemaphoreHandle_t s_mutex       = nullptr;
static bool            s_initialized   = false;
static volatile bool   s_playing       = false;
static volatile bool   s_stop_request  = false;
static volatile bool   s_looping       = false;
static SoundClip       s_pending_clip  = SoundClip::kChirpHappy;
static uint8_t         s_volume        = 60;  // 0-100, default 60%

// ---------------------------------------------------------------------------
// Resample 16kHz → 24kHz (3:2 ratio) using linear interpolation
//
// For every 2 input samples, we produce 3 output samples.
// Input positions: 0, 1, 2, 3, ...
// Output positions mapped to input: 0, 2/3, 4/3, 2, 8/3, 10/3, ...
//
// This is a simple but effective approach that avoids pulling in the
// full esp_ae_rate_cvt library for our small PCM clips.
// ---------------------------------------------------------------------------
static int resample_16k_to_24k(const int16_t* in, int in_samples, int16_t* out, int out_max) {
    // Output samples = in_samples * 3 / 2
    int out_samples = (in_samples * 3) / 2;
    if (out_samples > out_max) {
        out_samples = out_max;
    }

    for (int i = 0; i < out_samples; i++) {
        // Map output index to fractional input index
        // in_frac = i * 2.0 / 3.0
        int in_idx = (i * 2) / 3;
        int frac_num = (i * 2) % 3;  // Numerator of fraction (0, 1, 2) / 3

        if (in_idx >= in_samples - 1) {
            // Clamp to last sample
            out[i] = in[in_samples - 1];
        } else {
            // Linear interpolation
            int32_t s0 = in[in_idx];
            int32_t s1 = in[in_idx + 1];
            out[i] = (int16_t)(s0 + (s1 - s0) * frac_num / 3);
        }
    }

    return out_samples;
}

// ---------------------------------------------------------------------------
// Apply volume scaling to a buffer of samples
// ---------------------------------------------------------------------------
static void apply_volume(int16_t* buf, int samples, uint8_t volume) {
    if (volume >= 100) return;  // No scaling needed
    if (volume == 0) {
        memset(buf, 0, samples * sizeof(int16_t));
        return;
    }
    // Use fixed-point: volume/100 as (volume * 256) / 25600
    int32_t vol_scale = (int32_t)volume * 256 / 100;
    for (int i = 0; i < samples; i++) {
        buf[i] = (int16_t)(((int32_t)buf[i] * vol_scale) >> 8);
    }
}

// ---------------------------------------------------------------------------
// Audio playback task — pinned to Core 1
//
// Waits for play notifications, then reads PCM from mmap'd assets,
// resamples, and writes to the codec. Checks device state before every write.
// ---------------------------------------------------------------------------
static void mochi_audio_task(void* arg) {
    // Buffers are static: only one audio task exists, and placing 5KB+ on
    // a 6KB stack would leave no headroom for codec calls / vector alloc.
    static int16_t read_buf[READ_BUF_SAMPLES];
    static int16_t write_buf[WRITE_BUF_SAMPLES];

    ESP_LOGI(TAG, "Audio task started on Core %d", xPortGetCoreID());

    for (;;) {
        // Wait for a play or stop notification
        uint32_t notification = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notification, portMAX_DELAY);

        if (notification & NOTIFY_STOP) {
            s_playing = false;
            s_stop_request = false;
            continue;
        }

        if (!(notification & NOTIFY_PLAY)) {
            continue;
        }

        // Get the clip to play
        SoundClip clip;
        bool looping;
        uint8_t volume;
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            clip = s_pending_clip;
            looping = s_looping;
            volume = s_volume;
            s_stop_request = false;
            xSemaphoreGive(s_mutex);
        } else {
            ESP_LOGW(TAG, "Audio task: mutex timeout");
            continue;
        }

        // Look up PCM data in memory-mapped assets
        char asset_name[64];
        SoundClipAssetName(clip, asset_name, sizeof(asset_name));

        void* pcm_ptr = nullptr;
        size_t pcm_size = 0;
        if (!Assets::GetInstance().GetAssetData(asset_name, pcm_ptr, pcm_size)) {
            ESP_LOGW(TAG, "SOUND_MISSING: %s — skipping", asset_name);
            continue;
        }

        size_t total_samples = pcm_size / sizeof(int16_t);
        ESP_LOGI(TAG, "SOUND_PLAY: %s (loop=%d, vol=%d, samples=%u)",
                 SoundClipName(clip), looping, volume, (unsigned)total_samples);

        // Get codec
        AudioCodec* codec = Board::GetInstance().GetAudioCodec();
        if (!codec) {
            ESP_LOGE(TAG, "No audio codec available");
            continue;
        }

        // Log heap before playback
        size_t heap_before = esp_get_free_heap_size();

        do {  // Loop point for looping playback
            const int16_t* src = static_cast<const int16_t*>(pcm_ptr);
            size_t offset = 0;  // Sample offset into PCM data

            s_playing = true;

            while (!s_stop_request && offset < total_samples) {
                // Check state before every read+write cycle
                if (!is_mochi_audio_allowed()) {
                    ESP_LOGI(TAG, "SOUND_STOP: %s (state not allowed)", SoundClipName(clip));
                    s_stop_request = true;
                    break;
                }

                // Enable output if not already enabled
                if (!codec->output_enabled()) {
                    codec->EnableOutput(true);
                }

                // Copy a chunk of PCM data from mmap'd memory
                size_t samples_to_read = READ_BUF_SAMPLES;
                if (offset + samples_to_read > total_samples) {
                    samples_to_read = total_samples - offset;
                }
                memcpy(read_buf, src + offset, samples_to_read * sizeof(int16_t));
                offset += samples_to_read;

                // Apply volume
                apply_volume(read_buf, samples_to_read, volume);

                // Resample 16kHz → 24kHz
                int out_samples = resample_16k_to_24k(read_buf, samples_to_read, write_buf, WRITE_BUF_SAMPLES);

                // Write to codec (this blocks until DMA accepts the data)
                std::vector<int16_t> out_vec(write_buf, write_buf + out_samples);
                codec->OutputData(out_vec);
            }

        } while (looping && !s_stop_request);

        s_playing = false;
        s_stop_request = false;

        // Log heap after playback for leak detection
        size_t heap_after = esp_get_free_heap_size();
        ESP_LOGI(TAG, "SOUND_DONE: %s | HEAP: before=%u, after=%u, delta=%d",
                 SoundClipName(clip),
                 (unsigned)heap_before, (unsigned)heap_after,
                 (int)heap_after - (int)heap_before);
    }
}

// =============================================================================
// Public API
// =============================================================================

void mochi_audio_init() {
    if (s_initialized) {
        ESP_LOGW(TAG, "Mochi audio already initialized");
        return;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create audio mutex");
        return;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        mochi_audio_task,
        "mochi_snd",
        AUDIO_TASK_STACK_SIZE,
        nullptr,
        AUDIO_TASK_PRIORITY,
        &s_task_handle,
        AUDIO_TASK_CORE
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio task");
        vSemaphoreDelete(s_mutex);
        s_mutex = nullptr;
        return;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Sound engine initialized (PCM %dHz -> codec %dHz, task Core %d pri %d)",
             PCM_SAMPLE_RATE, CODEC_SAMPLE_RATE, AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY);
}

void mochi_audio_play(SoundClip clip) {
    if (!s_initialized) return;

    if (!is_mochi_audio_allowed()) {
        ESP_LOGD(TAG, "Play blocked: device state not idle");
        return;
    }

    // Stop any current playback first
    if (s_playing) {
        s_stop_request = true;
        // Give the task a moment to stop
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_pending_clip = clip;
        s_looping = false;
        xSemaphoreGive(s_mutex);
    }

    xTaskNotify(s_task_handle, NOTIFY_PLAY, eSetBits);
}

void mochi_audio_play_looping(SoundClip clip) {
    if (!s_initialized) return;

    if (!is_mochi_audio_allowed()) {
        ESP_LOGD(TAG, "Loop play blocked: device state not idle");
        return;
    }

    // Stop any current playback first
    if (s_playing) {
        s_stop_request = true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_pending_clip = clip;
        s_looping = true;
        xSemaphoreGive(s_mutex);
    }

    xTaskNotify(s_task_handle, NOTIFY_PLAY, eSetBits);
}

void mochi_audio_stop() {
    if (!s_initialized) return;

    if (s_playing) {
        s_stop_request = true;
        ESP_LOGI(TAG, "SOUND_STOP: requested");
        // Also notify the task in case it's waiting
        xTaskNotify(s_task_handle, NOTIFY_STOP, eSetBits);
    }
}

bool mochi_audio_is_playing() {
    return s_playing;
}

void mochi_audio_set_volume(uint8_t volume) {
    if (volume > 100) volume = 100;
    s_volume = volume;
    ESP_LOGI(TAG, "Volume set to %d%%", volume);
}

uint8_t mochi_audio_get_volume() {
    return s_volume;
}

bool is_mochi_audio_allowed() {
    DeviceState state = Application::GetInstance().GetDeviceState();
    // Only allow during idle (and connecting, for startup sounds)
    return (state == kDeviceStateIdle || state == kDeviceStateConnecting);
}

void mochi_audio_deinit() {
    if (!s_initialized) return;

    // Stop any playback
    s_stop_request = true;
    if (s_task_handle) {
        // Give task time to stop gracefully
        vTaskDelay(pdMS_TO_TICKS(50));
        vTaskDelete(s_task_handle);
        s_task_handle = nullptr;
    }

    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = nullptr;
    }

    s_initialized = false;
    s_playing = false;
    ESP_LOGI(TAG, "Sound engine deinitialized");
}
