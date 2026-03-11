#!/usr/bin/env python3
"""
Generate 14 Mochi PCM sound files — warm, natural, cute desk-pet sounds.
Format: 16kHz, mono, 16-bit signed little-endian.

Design principles (inspired by Tamagotchi, Furby, R2-D2):
  - Warm harmonic tones (not pure sine — adds 2nd/3rd harmonics)
  - Proper ADSR envelopes for organic feel
  - Subtle pitch wobble on sustained tones
  - Pentatonic-friendly note choices (pleasant, non-jarring)
  - Short and punchy for reactions, gentle for ambient sounds
  - Sounds like a tiny creature, not a synthesizer

Sounds:
  chirp_happy      : Quick warm two-note "boo-beep!" (rising)
  chirp_excited    : Rapid ascending cascade, sparkly
  purr_loop        : Layered low rumble with realistic AM (~250ms loop)
  yawn             : Slow descending slide with breathy noise
  yelp_scared      : Sharp "eep!" — snap attack, high, very short
  sigh_relieved    : Gentle descending exhale
  grunt_angry      : Short grumbly growl
  attention_melody : Three gentle ascending bell tones
  lonely_melody    : Two soft sad notes, slow and distant
  good_morning     : Bright gentle ascending chime
  good_night       : Soft descending lullaby
  dizzy            : Wobbly spinning pitch
  joy_burst        : Cascading upward sparkle burst
  confirm          : Single soft bell "ding"
"""

import struct
import math
import os
import random

SAMPLE_RATE = 16000
MAX_AMP = 16000  # Conservative to avoid clipping after harmonic sum

random.seed(42)  # Reproducible


# =============================================================================
# Waveform generators
# =============================================================================

def warm_tone(freq, t, harmonics=(1.0, 0.3, 0.12, 0.05)):
    """Warm harmonic tone — fundamental + overtones for body."""
    val = 0.0
    for i, amp in enumerate(harmonics):
        val += amp * math.sin(2 * math.pi * freq * (i + 1) * t)
    # Normalize by sum of harmonic amplitudes
    return val / sum(harmonics)


def soft_square(freq, t, softness=0.8):
    """Soft square wave — rounded edges, warm and fuzzy."""
    phase = (freq * t) % 1.0
    if phase < 0.5:
        return math.tanh(softness * 4)
    else:
        return -math.tanh(softness * 4)


def noise(amp=0.3):
    """White noise sample."""
    return amp * (random.random() * 2 - 1)


# =============================================================================
# Envelope generators
# =============================================================================

def adsr_envelope(i, n_samples, attack_ms=10, decay_ms=40, sustain=0.7, release_ms=50):
    """ADSR envelope returning amplitude 0-1."""
    attack_s = int(SAMPLE_RATE * attack_ms / 1000)
    decay_s = int(SAMPLE_RATE * decay_ms / 1000)
    release_s = int(SAMPLE_RATE * release_ms / 1000)
    release_start = n_samples - release_s

    if i < attack_s:
        return i / max(attack_s, 1)
    elif i < attack_s + decay_s:
        progress = (i - attack_s) / max(decay_s, 1)
        return 1.0 - (1.0 - sustain) * progress
    elif i < release_start:
        return sustain
    else:
        progress = (i - release_start) / max(release_s, 1)
        return sustain * (1.0 - progress)


def simple_fade(i, n_samples, fade_in_ms=8, fade_out_ms=15):
    """Simple fade in/out."""
    fade_in = int(SAMPLE_RATE * fade_in_ms / 1000)
    fade_out = int(SAMPLE_RATE * fade_out_ms / 1000)
    env = 1.0
    if i < fade_in:
        env = i / max(fade_in, 1)
    if i > n_samples - fade_out:
        env = min(env, (n_samples - i) / max(fade_out, 1))
    return env


# =============================================================================
# Sound building blocks
# =============================================================================

def gen_warm_note(freq, duration_ms, amplitude=MAX_AMP, attack_ms=8, decay_ms=30,
                  sustain=0.7, release_ms=40, vibrato_hz=0, vibrato_depth=0):
    """Generate a single warm note with ADSR and optional vibrato."""
    n = int(SAMPLE_RATE * duration_ms / 1000)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / SAMPLE_RATE
        f = freq
        if vibrato_hz > 0:
            f += vibrato_depth * math.sin(2 * math.pi * vibrato_hz * t)
        phase += f / SAMPLE_RATE
        val = warm_tone(1.0, phase)  # Use normalized phase
        env = adsr_envelope(i, n, attack_ms, decay_ms, sustain, release_ms)
        samples.append(int(val * env * amplitude))
    return samples


def gen_glide(freq_start, freq_end, duration_ms, amplitude=MAX_AMP,
              attack_ms=8, release_ms=30):
    """Warm gliding tone."""
    n = int(SAMPLE_RATE * duration_ms / 1000)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / n
        freq = freq_start + (freq_end - freq_start) * t
        phase += freq / SAMPLE_RATE
        val = warm_tone(1.0, phase)
        env = simple_fade(i, n, attack_ms, release_ms)
        samples.append(int(val * env * amplitude))
    return samples


def gen_silence(duration_ms):
    return [0] * int(SAMPLE_RATE * duration_ms / 1000)


def gen_bell(freq, duration_ms, amplitude=MAX_AMP):
    """Bell-like tone — quick attack, exponential decay, rich harmonics."""
    n = int(SAMPLE_RATE * duration_ms / 1000)
    samples = []
    # Bell harmonics: 1, 2, 3, 4.5 (inharmonic for shimmer)
    bell_harmonics = [1.0, 0.6, 0.25, 0.15]
    bell_ratios = [1.0, 2.0, 3.0, 4.5]
    for i in range(n):
        t = i / SAMPLE_RATE
        # Exponential decay (faster for higher partials)
        val = 0
        for amp, ratio in zip(bell_harmonics, bell_ratios):
            decay = math.exp(-t * (3 + ratio * 2))
            val += amp * decay * math.sin(2 * math.pi * freq * ratio * t)
        val /= sum(bell_harmonics)
        env = simple_fade(i, n, fade_in_ms=2, fade_out_ms=20)
        samples.append(int(val * env * amplitude))
    return samples


# =============================================================================
# Save helper
# =============================================================================

def save_pcm(filename, samples, output_dir):
    path = os.path.join(output_dir, filename)
    with open(path, 'wb') as f:
        for s in samples:
            s = max(-32767, min(32767, s))
            f.write(struct.pack('<h', s))
    dur = len(samples) / SAMPLE_RATE
    print(f"  {filename}: {len(samples)} samples, {dur:.2f}s, {len(samples)*2} bytes")
    return path


# =============================================================================
# Main — generate all 14 sounds
# =============================================================================

def main():
    output_dir = os.path.join(os.path.dirname(__file__), '..', 'spiffs_assets', 'sounds')
    os.makedirs(output_dir, exist_ok=True)
    print("Generating Mochi PCM sounds (16kHz, 16-bit LE, mono):\n")

    # -----------------------------------------------------------------------
    # 1. chirp_happy — Quick warm "boo-beep!" rising two-note
    #    C5 (523) → G5 (784), short gap, cute and snappy
    # -----------------------------------------------------------------------
    samples = (gen_warm_note(523, 90, attack_ms=5, decay_ms=20, sustain=0.6, release_ms=15) +
               gen_silence(25) +
               gen_warm_note(784, 110, attack_ms=5, decay_ms=20, sustain=0.65, release_ms=25))
    save_pcm('chirp_happy.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 2. chirp_excited — Rapid ascending sparkle cascade
    #    C5→E5→G5→C6→E6, getting brighter and shorter
    # -----------------------------------------------------------------------
    notes = [(523, 55), (659, 50), (784, 45), (1047, 40), (1319, 70)]
    samples = []
    for freq, dur in notes:
        samples += gen_warm_note(freq, dur, amplitude=int(MAX_AMP * 0.85),
                                 attack_ms=3, decay_ms=15, sustain=0.6, release_ms=10)
        samples += gen_silence(8)
    save_pcm('chirp_excited.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 3. purr_loop — Realistic cat-purr: layered low frequencies with AM
    #    ~250ms loop, seamless. Real purrs are 25-50Hz with amplitude modulation.
    # -----------------------------------------------------------------------
    dur_ms = 250
    n = int(SAMPLE_RATE * dur_ms / 1000)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        # Core purr: ~30Hz rumble with harmonics
        fundamental = 0.5 * math.sin(2 * math.pi * 28 * t)
        harmonic2 = 0.3 * math.sin(2 * math.pi * 56 * t)
        harmonic3 = 0.15 * math.sin(2 * math.pi * 84 * t)
        # Slight throat noise
        throat = 0.08 * noise()
        # Amplitude modulation at ~5Hz for the rhythmic "vrr-vrr-vrr"
        am = 0.55 + 0.45 * math.sin(2 * math.pi * 4.5 * t)
        val = (fundamental + harmonic2 + harmonic3 + throat) * am
        # Ensure seamless loop (crossfade first/last 10ms is handled by short
        # ramp but since it loops, we want phase-aligned — 28Hz * 0.25s = 7 cycles ✓)
        samples.append(int(val * MAX_AMP * 0.7))
    save_pcm('purr_loop.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 4. yawn — Slow descending slide with breathy overlay
    #    A4(440) → D4(294), 700ms, gentle and sleepy
    # -----------------------------------------------------------------------
    dur_ms = 700
    n = int(SAMPLE_RATE * dur_ms / 1000)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / n
        freq = 440 - 146 * t  # Linear descent
        phase += freq / SAMPLE_RATE
        tone = warm_tone(1.0, phase) * 0.7
        breath = noise(0.12) * (1.0 - t * 0.5)  # Breath fades out
        val = (tone + breath)
        env = adsr_envelope(i, n, attack_ms=30, decay_ms=100, sustain=0.5, release_ms=150)
        samples.append(int(val * env * MAX_AMP * 0.6))
    save_pcm('yawn.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 5. yelp_scared — Sharp "eep!" snap, very short
    #    High B5(988) with fast attack, 80ms total
    # -----------------------------------------------------------------------
    samples = gen_warm_note(988, 80, amplitude=int(MAX_AMP * 0.9),
                            attack_ms=2, decay_ms=15, sustain=0.5, release_ms=20)
    save_pcm('yelp_scared.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 6. sigh_relieved — Gentle descending exhale
    #    G4(392) → D4(294), 400ms, soft with breath texture
    # -----------------------------------------------------------------------
    dur_ms = 400
    n = int(SAMPLE_RATE * dur_ms / 1000)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / n
        freq = 392 - 98 * t
        phase += freq / SAMPLE_RATE
        tone = warm_tone(1.0, phase) * 0.5
        breath = noise(0.15) * (0.3 + 0.7 * (1.0 - t))
        val = tone + breath
        env = adsr_envelope(i, n, attack_ms=20, decay_ms=60, sustain=0.45, release_ms=100)
        samples.append(int(val * env * MAX_AMP * 0.5))
    save_pcm('sigh_relieved.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 7. grunt_angry — Short grumbly growl
    #    Low A2(110) with distorted harmonics, 180ms
    # -----------------------------------------------------------------------
    dur_ms = 180
    n = int(SAMPLE_RATE * dur_ms / 1000)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / SAMPLE_RATE
        phase += 110 / SAMPLE_RATE
        # Heavy harmonics for growl character
        val = (0.5 * math.sin(2 * math.pi * phase) +
               0.35 * math.sin(2 * math.pi * phase * 2) +
               0.25 * math.sin(2 * math.pi * phase * 3) +
               0.15 * math.sin(2 * math.pi * phase * 5))
        # Slight distortion via soft clipping
        val = math.tanh(val * 1.5) * 0.7
        # Add grit
        val += noise(0.06)
        env = adsr_envelope(i, n, attack_ms=5, decay_ms=30, sustain=0.65, release_ms=40)
        samples.append(int(val * env * MAX_AMP * 0.75))
    save_pcm('grunt_angry.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 8. attention_melody — Three gentle ascending bell tones
    #    C5→E5→G5, spaced for a friendly "hey listen!"
    # -----------------------------------------------------------------------
    samples = (gen_bell(523, 180, int(MAX_AMP * 0.6)) +
               gen_silence(30) +
               gen_bell(659, 180, int(MAX_AMP * 0.65)) +
               gen_silence(30) +
               gen_bell(784, 250, int(MAX_AMP * 0.7)))
    save_pcm('attention_melody.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 9. lonely_melody — Two soft sad notes, slow and distant
    #    E4(330) → C4(262), minor feel, 600ms total
    # -----------------------------------------------------------------------
    samples = (gen_warm_note(330, 250, amplitude=int(MAX_AMP * 0.4),
                             attack_ms=20, decay_ms=60, sustain=0.4, release_ms=60,
                             vibrato_hz=4, vibrato_depth=3) +
               gen_silence(50) +
               gen_warm_note(262, 300, amplitude=int(MAX_AMP * 0.35),
                             attack_ms=25, decay_ms=80, sustain=0.35, release_ms=80,
                             vibrato_hz=4, vibrato_depth=3))
    save_pcm('lonely_melody.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 10. good_morning — Bright ascending chime
    #     C5→D5→E5→G5, crisp bell tones
    # -----------------------------------------------------------------------
    notes = [(523, 100), (587, 100), (659, 100), (784, 160)]
    samples = []
    for freq, dur in notes:
        samples += gen_bell(freq, dur, int(MAX_AMP * 0.6))
        samples += gen_silence(15)
    save_pcm('good_morning.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 11. good_night — Soft descending lullaby
    #     G4→E4→C4, gentle warm tones fading out
    # -----------------------------------------------------------------------
    levels = [0.4, 0.33, 0.27]
    freqs = [392, 330, 262]
    durs = [180, 200, 280]
    samples = []
    for freq, dur, lvl in zip(freqs, durs, levels):
        samples += gen_warm_note(freq, dur, amplitude=int(MAX_AMP * lvl),
                                 attack_ms=25, decay_ms=40, sustain=0.4, release_ms=60,
                                 vibrato_hz=3, vibrato_depth=2)
        samples += gen_silence(40)
    save_pcm('good_night.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 12. dizzy — Wobbly spinning pitch modulation
    #     C5 center with wide vibrato (±60Hz at 6Hz), 350ms
    # -----------------------------------------------------------------------
    dur_ms = 350
    n = int(SAMPLE_RATE * dur_ms / 1000)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / SAMPLE_RATE
        wobble = 60 * math.sin(2 * math.pi * 6 * t)
        freq = 523 + wobble
        phase += freq / SAMPLE_RATE
        val = warm_tone(1.0, phase)
        env = adsr_envelope(i, n, attack_ms=10, decay_ms=30, sustain=0.6, release_ms=60)
        samples.append(int(val * env * MAX_AMP * 0.65))
    save_pcm('dizzy.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 13. joy_burst — Cascading upward sparkle burst
    #     Rapid ascending bell notes, getting shorter and brighter
    # -----------------------------------------------------------------------
    notes = [(523, 40), (659, 35), (784, 35), (1047, 30), (1319, 55)]
    samples = []
    for idx, (freq, dur) in enumerate(notes):
        amp = int(MAX_AMP * (0.5 + idx * 0.08))
        samples += gen_bell(freq, dur, amp)
        samples += gen_silence(5)
    save_pcm('joy_burst.pcm', samples, output_dir)

    # -----------------------------------------------------------------------
    # 14. confirm — Single soft bell "ding"
    #     A5 (880), clean and brief, 100ms
    # -----------------------------------------------------------------------
    samples = gen_bell(880, 100, int(MAX_AMP * 0.5))
    save_pcm('confirm.pcm', samples, output_dir)

    print(f"\nAll 14 PCM files generated in {os.path.abspath(output_dir)}")


if __name__ == '__main__':
    main()
