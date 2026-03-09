#ifndef MOCHI_EMOTION_H
#define MOCHI_EMOTION_H

// =============================================================================
// mochi_emotion.h — Mochi emotion enum + string/emoji mappers
//
// Maps Xiaozhi WebSocket emotion strings and emoji characters to Mochi's
// 15 GIF-based emotion states. Every incoming emotion packet goes through
// EmotionFromString() first, then EmotionFromEmoji() as fallback.
//
// GIF files: /spiffs/gifs/<emotion_name>.gif (150x150 px)
// =============================================================================

#include <string>
#include <cstring>

// ---------------------------------------------------------------------------
// 15 Mochi emotion states — each maps to a GIF file on SPIFFS
// ---------------------------------------------------------------------------
enum class MochiEmotion {
    kIdle = 0,      // Default resting face
    kListening,     // Ears perked, attentive
    kThinking,      // Processing dots / swirl
    kTalking,       // Mouth moving, engaged
    kHappy,         // Smile, soft eyes
    kExcited,       // Big eyes, bouncy
    kLoved,         // Hearts, blushing
    kSad,           // Droopy eyes, frown
    kAngry,         // Furrowed brow, red
    kScared,        // Wide eyes, shaking
    kStartled,      // Jump scare, brief
    kRelieved,      // Exhale, relaxed
    kSleepy,        // Droopy lids, yawn
    kConfused,      // Question mark, tilted
    kBored,         // Half-lidded, sighing

    kCount          // Sentinel — must be last
};

// ---------------------------------------------------------------------------
// Emotion name strings (lowercase, match GIF filenames)
// ---------------------------------------------------------------------------
inline const char* MochiEmotionName(MochiEmotion e) {
    switch (e) {
        case MochiEmotion::kIdle:      return "idle";
        case MochiEmotion::kListening: return "listening";
        case MochiEmotion::kThinking:  return "thinking";
        case MochiEmotion::kTalking:   return "talking";
        case MochiEmotion::kHappy:     return "happy";
        case MochiEmotion::kExcited:   return "excited";
        case MochiEmotion::kLoved:     return "loved";
        case MochiEmotion::kSad:       return "sad";
        case MochiEmotion::kAngry:     return "angry";
        case MochiEmotion::kScared:    return "scared";
        case MochiEmotion::kStartled:  return "startled";
        case MochiEmotion::kRelieved:  return "relieved";
        case MochiEmotion::kSleepy:    return "sleepy";
        case MochiEmotion::kConfused:  return "confused";
        case MochiEmotion::kBored:     return "bored";
        default:                       return "idle";
    }
}

// ---------------------------------------------------------------------------
// EmotionFromString — maps Xiaozhi "emotion" field values to MochiEmotion
//
// The server sends emotion strings like "smile", "sad", "angry", "neutral".
// Unknown strings fall back to kIdle.
// ---------------------------------------------------------------------------
inline MochiEmotion EmotionFromString(const std::string& s) {
    // --- Direct matches to Mochi emotion names ---
    if (s == "idle")       return MochiEmotion::kIdle;
    if (s == "listening")  return MochiEmotion::kListening;
    if (s == "thinking")   return MochiEmotion::kThinking;
    if (s == "talking")    return MochiEmotion::kTalking;
    if (s == "happy")      return MochiEmotion::kHappy;
    if (s == "excited")    return MochiEmotion::kExcited;
    if (s == "loved")      return MochiEmotion::kLoved;
    if (s == "sad")        return MochiEmotion::kSad;
    if (s == "angry")      return MochiEmotion::kAngry;
    if (s == "scared")     return MochiEmotion::kScared;
    if (s == "startled")   return MochiEmotion::kStartled;
    if (s == "relieved")   return MochiEmotion::kRelieved;
    if (s == "sleepy")     return MochiEmotion::kSleepy;
    if (s == "confused")   return MochiEmotion::kConfused;
    if (s == "bored")      return MochiEmotion::kBored;

    // --- Xiaozhi server emotion strings (mapped to closest Mochi state) ---
    if (s == "smile")      return MochiEmotion::kHappy;      // smile → happy
    if (s == "laugh")      return MochiEmotion::kExcited;     // laugh → excited
    if (s == "neutral")    return MochiEmotion::kIdle;        // neutral → idle
    if (s == "cry")        return MochiEmotion::kSad;         // cry → sad
    if (s == "surprise")   return MochiEmotion::kStartled;    // surprise → startled
    if (s == "fear")       return MochiEmotion::kScared;      // fear → scared
    if (s == "disgust")    return MochiEmotion::kAngry;       // disgust → angry
    if (s == "contempt")   return MochiEmotion::kAngry;       // contempt → angry
    if (s == "love")       return MochiEmotion::kLoved;       // love → loved
    if (s == "joy")        return MochiEmotion::kExcited;     // joy → excited
    if (s == "relief")     return MochiEmotion::kRelieved;    // relief → relieved
    if (s == "calm")       return MochiEmotion::kIdle;        // calm → idle
    if (s == "trust")      return MochiEmotion::kHappy;       // trust → happy
    if (s == "interest")   return MochiEmotion::kListening;   // interest → listening
    if (s == "anticipation") return MochiEmotion::kExcited;   // anticipation → excited
    if (s == "wonder")     return MochiEmotion::kConfused;    // wonder → confused
    if (s == "sleepy")     return MochiEmotion::kSleepy;      // sleepy → sleepy
    if (s == "tired")      return MochiEmotion::kSleepy;      // tired → sleepy
    if (s == "boredom")    return MochiEmotion::kBored;       // boredom → bored

    // --- Font Awesome / icon emotion names (used by Xiaozhi alerts) ---
    if (s == "circle_xmark")           return MochiEmotion::kAngry;
    if (s == "triangle_exclamation")   return MochiEmotion::kScared;
    if (s == "cloud_slash")            return MochiEmotion::kSad;
    if (s == "cloud_arrow_down")       return MochiEmotion::kThinking;
    if (s == "download")               return MochiEmotion::kThinking;
    if (s == "link")                   return MochiEmotion::kThinking;
    if (s == "microchip_ai")           return MochiEmotion::kExcited;

    // Unknown → idle (safe default)
    return MochiEmotion::kIdle;
}

// ---------------------------------------------------------------------------
// EmotionFromEmoji — maps emoji characters to MochiEmotion
//
// The LLM sometimes sends emoji in the "text" field alongside the emotion
// string. This provides a secondary mapping path.
// Unknown emoji fall back to kIdle.
// ---------------------------------------------------------------------------
inline MochiEmotion EmotionFromEmoji(const std::string& emoji) {
    if (emoji.empty()) return MochiEmotion::kIdle;

    // --- Happy / Smile family ---
    if (emoji == "\xF0\x9F\x98\x8A") return MochiEmotion::kHappy;    // 😊 smiling face with smiling eyes
    if (emoji == "\xF0\x9F\x98\x80") return MochiEmotion::kHappy;    // 😀 grinning face
    if (emoji == "\xF0\x9F\x98\x83") return MochiEmotion::kHappy;    // 😃 grinning face with big eyes
    if (emoji == "\xF0\x9F\x98\x84") return MochiEmotion::kHappy;    // 😄 grinning face with smiling eyes
    if (emoji == "\xF0\x9F\x98\x81") return MochiEmotion::kHappy;    // 😁 beaming face with smiling eyes
    if (emoji == "\xF0\x9F\x99\x82") return MochiEmotion::kHappy;    // 🙂 slightly smiling face
    if (emoji == "\xE2\x98\xBA\xEF\xB8\x8F") return MochiEmotion::kHappy; // ☺️ smiling face

    // --- Excited / Laughing ---
    if (emoji == "\xF0\x9F\x98\x86") return MochiEmotion::kExcited;  // 😆 grinning squinting face
    if (emoji == "\xF0\x9F\x98\x82") return MochiEmotion::kExcited;  // 😂 face with tears of joy
    if (emoji == "\xF0\x9F\xA4\xA3") return MochiEmotion::kExcited;  // 🤣 rolling on floor laughing
    if (emoji == "\xF0\x9F\x8E\x89") return MochiEmotion::kExcited;  // 🎉 party popper
    if (emoji == "\xF0\x9F\x98\x9C") return MochiEmotion::kExcited;  // 😜 winking face with tongue

    // --- Love / Heart ---
    if (emoji == "\xF0\x9F\x98\x8D") return MochiEmotion::kLoved;    // 😍 smiling face with heart-eyes
    if (emoji == "\xF0\x9F\xA5\xB0") return MochiEmotion::kLoved;    // 🥰 smiling face with hearts
    if (emoji == "\xE2\x9D\xA4\xEF\xB8\x8F") return MochiEmotion::kLoved; // ❤️ red heart
    if (emoji == "\xF0\x9F\x92\x95") return MochiEmotion::kLoved;    // 💕 two hearts
    if (emoji == "\xF0\x9F\x98\x98") return MochiEmotion::kLoved;    // 😘 face blowing a kiss

    // --- Sad / Crying ---
    if (emoji == "\xF0\x9F\x98\xA2") return MochiEmotion::kSad;      // 😢 crying face
    if (emoji == "\xF0\x9F\x98\xAD") return MochiEmotion::kSad;      // 😭 loudly crying face
    if (emoji == "\xF0\x9F\x98\x9E") return MochiEmotion::kSad;      // 😞 disappointed face
    if (emoji == "\xF0\x9F\x98\x94") return MochiEmotion::kSad;      // 😔 pensive face
    if (emoji == "\xF0\x9F\x98\xA5") return MochiEmotion::kSad;      // 😥 sad but relieved face
    if (emoji == "\xF0\x9F\x98\xBF") return MochiEmotion::kSad;      // 😿 crying cat

    // --- Angry ---
    if (emoji == "\xF0\x9F\x98\xA0") return MochiEmotion::kAngry;    // 😠 angry face
    if (emoji == "\xF0\x9F\x98\xA1") return MochiEmotion::kAngry;    // 😡 pouting face
    if (emoji == "\xF0\x9F\xA4\xAC") return MochiEmotion::kAngry;    // 🤬 face with symbols on mouth
    if (emoji == "\xF0\x9F\x92\xA2") return MochiEmotion::kAngry;    // 💢 anger symbol

    // --- Scared / Fear ---
    if (emoji == "\xF0\x9F\x98\xB1") return MochiEmotion::kScared;   // 😱 face screaming in fear
    if (emoji == "\xF0\x9F\x98\xA8") return MochiEmotion::kScared;   // 😨 fearful face
    if (emoji == "\xF0\x9F\x98\xB0") return MochiEmotion::kScared;   // 😰 anxious face with sweat

    // --- Startled / Surprise ---
    if (emoji == "\xF0\x9F\x98\xB2") return MochiEmotion::kStartled;  // 😲 astonished face
    if (emoji == "\xF0\x9F\x98\xAE") return MochiEmotion::kStartled;  // 😮 face with open mouth
    if (emoji == "\xF0\x9F\x98\xAF") return MochiEmotion::kStartled;  // 😯 hushed face
    if (emoji == "\xE2\x9D\x97")     return MochiEmotion::kStartled;  // ❗ exclamation mark

    // --- Relieved ---
    if (emoji == "\xF0\x9F\x98\x8C") return MochiEmotion::kRelieved;  // 😌 relieved face
    if (emoji == "\xF0\x9F\x98\xAE\xE2\x80\x8D\xF0\x9F\x92\xA8") return MochiEmotion::kRelieved; // 😮‍💨 face exhaling

    // --- Sleepy ---
    if (emoji == "\xF0\x9F\x98\xB4") return MochiEmotion::kSleepy;   // 😴 sleeping face
    if (emoji == "\xF0\x9F\xA5\xB1") return MochiEmotion::kSleepy;   // 🥱 yawning face
    if (emoji == "\xF0\x9F\x98\xAA") return MochiEmotion::kSleepy;   // 😪 sleepy face
    if (emoji == "\xF0\x9F\x92\xA4") return MochiEmotion::kSleepy;   // 💤 zzz

    // --- Confused ---
    if (emoji == "\xF0\x9F\x98\x95") return MochiEmotion::kConfused;  // 😕 confused face
    if (emoji == "\xF0\x9F\xA4\x94") return MochiEmotion::kConfused;  // 🤔 thinking face (maps to confused visually)
    if (emoji == "\xF0\x9F\x98\x96") return MochiEmotion::kConfused;  // 😖 confounded face
    if (emoji == "\xE2\x9D\x93")     return MochiEmotion::kConfused;  // ❓ question mark

    // --- Thinking (for 🤔 as primary thinking rather than confused) ---
    if (emoji == "\xF0\x9F\x92\xAD") return MochiEmotion::kThinking;  // 💭 thought balloon
    if (emoji == "\xF0\x9F\xA7\x90") return MochiEmotion::kThinking;  // 🧐 face with monocle

    // --- Bored ---
    if (emoji == "\xF0\x9F\x98\x91") return MochiEmotion::kBored;    // 😑 expressionless face
    if (emoji == "\xF0\x9F\x98\x90") return MochiEmotion::kBored;    // 😐 neutral face
    if (emoji == "\xF0\x9F\xA5\xB1") return MochiEmotion::kBored;    // 🥱 yawning face (also maps to sleepy above; first match wins)

    // --- Idle / Neutral ---
    if (emoji == "\xF0\x9F\x98\xB6") return MochiEmotion::kIdle;     // 😶 face without mouth

    // Unknown emoji → idle (safe default)
    return MochiEmotion::kIdle;
}

// ---------------------------------------------------------------------------
// GIF path helper — returns SPIFFS path for an emotion's GIF
// Format: /spiffs/gifs/<emotion_name>.gif
// ---------------------------------------------------------------------------
inline const char* MochiEmotionGifPath(MochiEmotion e, char* buf, size_t buf_size) {
    snprintf(buf, buf_size, "/spiffs/gifs/%s.gif", MochiEmotionName(e));
    return buf;
}

#endif // MOCHI_EMOTION_H
