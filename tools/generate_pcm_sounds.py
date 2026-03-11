#!/usr/bin/env python3
"""
Generate 14 Mochi PCM sound files for testing.
Format: 16kHz, mono, 16-bit signed little-endian.

Each sound has a distinct character to be easily identifiable:
- chirp_happy:       Rising two-note chirp (C5→E5)
- chirp_excited:     Fast ascending arpeggio (C5→E5→G5→C6)
- purr_loop:         Low rumble with slight modulation (~200ms loop)
- yawn:              Slow descending glide (A4→D4)
- yelp_scared:       Sharp high burst (B5, very short)
- sigh_relieved:     Descending breathy tone (G4→D4)
- grunt_angry:       Low harsh buzz (A2, short)
- attention_melody:  Three ascending notes (C5→E5→G5)
- lonely_melody:     Sad two-note (E4→C4, slow)
- good_morning:      Bright ascending scale snippet
- good_night:        Gentle descending lullaby notes
- dizzy:             Wobbling pitch (C5 with vibrato)
- joy_burst:         Rapid ascending sparkle
- confirm:           Single soft click/beep (short)
"""

import struct
import math
import os

SAMPLE_RATE = 16000
MAX_AMP = 20000  # Keep below clipping, leave headroom for volume scaling


def generate_tone(freq, duration_ms, amplitude=MAX_AMP, fade_ms=10):
    """Generate a sine wave tone with fade in/out."""
    n_samples = int(SAMPLE_RATE * duration_ms / 1000)
    fade_samples = int(SAMPLE_RATE * fade_ms / 1000)
    samples = []
    for i in range(n_samples):
        t = i / SAMPLE_RATE
        val = amplitude * math.sin(2 * math.pi * freq * t)
        # Fade in
        if i < fade_samples:
            val *= i / fade_samples
        # Fade out
        if i > n_samples - fade_samples:
            val *= (n_samples - i) / fade_samples
        samples.append(int(val))
    return samples


def generate_glide(freq_start, freq_end, duration_ms, amplitude=MAX_AMP):
    """Generate a frequency glide (portamento)."""
    n_samples = int(SAMPLE_RATE * duration_ms / 1000)
    fade_samples = int(SAMPLE_RATE * 10 / 1000)
    samples = []
    phase = 0
    for i in range(n_samples):
        t = i / n_samples
        freq = freq_start + (freq_end - freq_start) * t
        phase += 2 * math.pi * freq / SAMPLE_RATE
        val = amplitude * math.sin(phase)
        if i < fade_samples:
            val *= i / fade_samples
        if i > n_samples - fade_samples:
            val *= (n_samples - i) / fade_samples
        samples.append(int(val))
    return samples


def generate_silence(duration_ms):
    """Generate silence."""
    return [0] * int(SAMPLE_RATE * duration_ms / 1000)


def save_pcm(filename, samples, output_dir):
    """Save samples as raw PCM (16-bit LE mono)."""
    path = os.path.join(output_dir, filename)
    with open(path, 'wb') as f:
        for s in samples:
            s = max(-32767, min(32767, s))
            f.write(struct.pack('<h', s))
    print(f"  {filename}: {len(samples)} samples, {len(samples)/SAMPLE_RATE:.2f}s, {len(samples)*2} bytes")
    return path


def main():
    output_dir = os.path.join(os.path.dirname(__file__), '..', 'spiffs_assets', 'sounds')
    os.makedirs(output_dir, exist_ok=True)

    print("Generating Mochi PCM sound files (16kHz, 16-bit LE, mono):\n")

    # 1. chirp_happy — Rising two-note chirp (C5→E5), 300ms
    samples = generate_tone(523, 120) + generate_silence(30) + generate_tone(659, 150)
    save_pcm('chirp_happy.pcm', samples, output_dir)

    # 2. chirp_excited — Fast ascending arpeggio, 400ms
    samples = (generate_tone(523, 80) + generate_silence(10) +
               generate_tone(659, 80) + generate_silence(10) +
               generate_tone(784, 80) + generate_silence(10) +
               generate_tone(1047, 120))
    save_pcm('chirp_excited.pcm', samples, output_dir)

    # 3. purr_loop — Low rumble ~200ms (designed to loop seamlessly)
    n = int(SAMPLE_RATE * 0.2)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        # Low frequency with amplitude modulation for "purr" character
        val = MAX_AMP * 0.6 * math.sin(2 * math.pi * 80 * t)
        val *= 0.7 + 0.3 * math.sin(2 * math.pi * 5 * t)  # Slow modulation
        samples.append(int(val))
    save_pcm('purr_loop.pcm', samples, output_dir)

    # 4. yawn — Slow descending glide A4→D4, 800ms
    samples = generate_glide(440, 294, 800, int(MAX_AMP * 0.7))
    save_pcm('yawn.pcm', samples, output_dir)

    # 5. yelp_scared — Sharp high burst, 100ms
    samples = generate_tone(988, 100, MAX_AMP)
    save_pcm('yelp_scared.pcm', samples, output_dir)

    # 6. sigh_relieved — Descending tone G4→D4, 500ms
    samples = generate_glide(392, 294, 500, int(MAX_AMP * 0.5))
    save_pcm('sigh_relieved.pcm', samples, output_dir)

    # 7. grunt_angry — Low harsh buzz A2, 200ms
    n = int(SAMPLE_RATE * 0.2)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        # Square-ish wave for harsh character
        val = MAX_AMP * 0.8 * math.copysign(1, math.sin(2 * math.pi * 110 * t))
        # Add some harmonics
        val += MAX_AMP * 0.3 * math.sin(2 * math.pi * 220 * t)
        fade = 1.0
        if i < 200:
            fade = i / 200
        if i > n - 200:
            fade = (n - i) / 200
        samples.append(int(val * fade))
    save_pcm('grunt_angry.pcm', samples, output_dir)

    # 8. attention_melody — Three ascending notes C5→E5→G5, 500ms
    samples = (generate_tone(523, 130) + generate_silence(20) +
               generate_tone(659, 130) + generate_silence(20) +
               generate_tone(784, 180))
    save_pcm('attention_melody.pcm', samples, output_dir)

    # 9. lonely_melody — Sad two-note E4→C4, 700ms
    samples = generate_tone(330, 300, int(MAX_AMP * 0.5)) + generate_silence(50) + generate_tone(262, 350, int(MAX_AMP * 0.4))
    save_pcm('lonely_melody.pcm', samples, output_dir)

    # 10. good_morning — Bright ascending C5→D5→E5→G5, 600ms
    samples = (generate_tone(523, 120) + generate_silence(15) +
               generate_tone(587, 120) + generate_silence(15) +
               generate_tone(659, 120) + generate_silence(15) +
               generate_tone(784, 180))
    save_pcm('good_morning.pcm', samples, output_dir)

    # 11. good_night — Gentle descending G4→E4→C4, 700ms
    samples = (generate_tone(392, 180, int(MAX_AMP * 0.4)) + generate_silence(30) +
               generate_tone(330, 180, int(MAX_AMP * 0.3)) + generate_silence(30) +
               generate_tone(262, 280, int(MAX_AMP * 0.25)))
    save_pcm('good_night.pcm', samples, output_dir)

    # 12. dizzy — Wobbling pitch C5 with vibrato, 400ms
    n = int(SAMPLE_RATE * 0.4)
    samples = []
    phase = 0
    for i in range(n):
        t = i / SAMPLE_RATE
        freq = 523 + 50 * math.sin(2 * math.pi * 8 * t)  # 8Hz vibrato
        phase += 2 * math.pi * freq / SAMPLE_RATE
        val = MAX_AMP * 0.7 * math.sin(phase)
        fade = 1.0
        if i < 300:
            fade = i / 300
        if i > n - 300:
            fade = (n - i) / 300
        samples.append(int(val * fade))
    save_pcm('dizzy.pcm', samples, output_dir)

    # 13. joy_burst — Rapid ascending sparkle, 300ms
    samples = []
    for note_idx, freq in enumerate([523, 659, 784, 1047, 1319]):
        dur = 40 + note_idx * 10
        samples += generate_tone(freq, dur, int(MAX_AMP * (0.6 + note_idx * 0.08)))
        if note_idx < 4:
            samples += generate_silence(5)
    save_pcm('joy_burst.pcm', samples, output_dir)

    # 14. confirm — Single soft beep, 80ms
    samples = generate_tone(880, 80, int(MAX_AMP * 0.4))
    save_pcm('confirm.pcm', samples, output_dir)

    print(f"\nAll 14 PCM files generated in {os.path.abspath(output_dir)}")


if __name__ == '__main__':
    main()
