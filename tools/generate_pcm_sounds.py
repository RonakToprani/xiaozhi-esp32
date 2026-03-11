#!/usr/bin/env python3
"""
Generate 14 Mochi PCM sound files — bubble pops, water drops, vocal chirps.
Format: 16kHz, mono, 16-bit signed little-endian.

Sound design v4 — DRAMATICALLY different from beeps/tones:
  - Bubble pops: high→low frequency sweep with exponential decay ("blip!")
  - Vocal chirps: dual-frequency formant pairs that mimic tiny "boo!"/"pip!"
  - Water-drop plucks: sharp downward pitch sweep, sounds like dripping
  - Soft filtered noise for breathy/purry textures
  - NO sustained tones, NO rising beeps — everything is plucky/poppy/bubbly

Think: water bubbles, Kirby's pop sounds, Animal Crossing item pickup
"""

import struct
import math
import os
import random

SAMPLE_RATE = 16000
MAX_AMP = 13000
random.seed(42)


# =============================================================================
# Core sound building blocks
# =============================================================================

def bubble_pop(freq_start, freq_end, duration_ms, amplitude=1.0, decay_speed=5.0):
    """A bubble pop: frequency sweeps down quickly with exponential decay.
    This is the core 'cute' sound — like a tiny water bubble popping."""
    n = int(SAMPLE_RATE * duration_ms / 1000)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / n
        # Exponential frequency sweep (fast start, slow settle)
        freq = freq_end + (freq_start - freq_end) * math.exp(-decay_speed * t)
        phase += freq / SAMPLE_RATE
        # Pure-ish sine (tiny 2nd harmonic for warmth)
        val = math.sin(2 * math.pi * phase) + 0.08 * math.sin(2 * math.pi * 2 * phase)
        # Pop envelope: instant attack, smooth exponential decay
        fade_in = min(1.0, i / max(int(SAMPLE_RATE * 0.001), 1))  # 1ms click guard
        env = fade_in * math.exp(-decay_speed * 0.8 * t)
        samples.append(val * env * amplitude * MAX_AMP)
    return samples


def vocal_chirp(base_freq, formant_freq, duration_ms, amplitude=1.0):
    """Two-frequency formant pair — sounds like a tiny creature voice.
    base_freq is the 'pitch', formant_freq is the 'vowel color'.
    The interaction creates a vocal-like quality."""
    n = int(SAMPLE_RATE * duration_ms / 1000)
    samples = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(n):
        t = i / n
        # Carrier (base pitch) with slight pitch bend
        f1 = base_freq * (1.0 + 0.03 * math.sin(math.pi * t))  # Tiny arch
        phase1 += f1 / SAMPLE_RATE
        # Formant (vowel resonance) - amplitude modulated by carrier
        phase2 += formant_freq / SAMPLE_RATE
        carrier = math.sin(2 * math.pi * phase1)
        formant = 0.4 * math.sin(2 * math.pi * phase2)
        # Ring-mod-like interaction gives vocal quality
        val = carrier * 0.7 + carrier * formant * 0.5 + formant * 0.15
        # Smooth pop envelope
        fade_in = min(1.0, i / max(int(SAMPLE_RATE * 0.002), 1))
        env = fade_in * math.exp(-3.5 * t)
        samples.append(val * env * amplitude * MAX_AMP)
    return samples


def water_drop(freq, duration_ms, amplitude=1.0):
    """Water drop pluck: sharp attack, rapid pitch drop, quick decay.
    Sounds like a tiny raindrop or xylophone hit."""
    n = int(SAMPLE_RATE * duration_ms / 1000)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / n
        # Pitch drops quickly at start then stabilizes
        f = freq * (1.0 + 0.4 * math.exp(-15 * t))
        phase += f / SAMPLE_RATE
        # Clean sine — water drops are pure-toned
        val = math.sin(2 * math.pi * phase)
        # Sharp attack with ring (water drop resonance)
        env = math.exp(-4.0 * t) * (1.0 + 0.3 * math.exp(-20 * t))
        fade_in = min(1.0, i / max(int(SAMPLE_RATE * 0.001), 1))
        samples.append(val * fade_in * env * amplitude * MAX_AMP)
    return samples


def soft_noise_burst(duration_ms, amplitude=0.3):
    """Shaped noise — like a soft breath or puff."""
    n = int(SAMPLE_RATE * duration_ms / 1000)
    samples = []
    for i in range(n):
        t = i / n
        val = (random.random() * 2 - 1)
        env = math.exp(-5 * t)  # Quick fade
        samples.append(val * env * amplitude * MAX_AMP)
    return samples


def silence(ms):
    return [0] * int(SAMPLE_RATE * ms / 1000)


# =============================================================================
# Save helper
# =============================================================================

def save_pcm(filename, samples, output_dir):
    path = os.path.join(output_dir, filename)
    with open(path, 'wb') as f:
        for s in samples:
            s = max(-32767, min(32767, int(s)))
            f.write(struct.pack('<h', s))
    dur = len(samples) / SAMPLE_RATE
    print(f"  {filename}: {len(samples)} samples, {dur:.3f}s, {len(samples)*2} bytes")


# =============================================================================
# 14 Sound generators
# =============================================================================

def gen_chirp_happy():
    """THE main tap sound: a cute bubble pop pair.
    Two quick bubble pops ascending — 'buh-BLIP!' like a happy water drop.
    First pop is low and soft (setup), second is higher and brighter (payoff)."""
    return (
        bubble_pop(500, 350, 60, amplitude=0.5, decay_speed=6) +
        silence(15) +
        bubble_pop(900, 550, 100, amplitude=0.85, decay_speed=4.5)
    )


def gen_chirp_excited():
    """Rapid ascending bubble cascade — bubbly excitement!
    Each bubble pops higher and brighter than the last."""
    pops = [
        (600, 400, 45, 0.55),   # freq_start, freq_end, dur_ms, amp
        (750, 480, 42, 0.60),
        (900, 560, 38, 0.65),
        (1050, 640, 35, 0.70),
        (1200, 720, 55, 0.80),   # Last one rings longer
    ]
    samples = []
    for fs, fe, dur, amp in pops:
        samples += bubble_pop(fs, fe, dur, amplitude=amp, decay_speed=5)
        samples += silence(10)
    return samples


def gen_purr_loop():
    """Warm rumble — low frequencies with gentle pulsing.
    Less synthetic: uses filtered noise layered with sub-bass for texture."""
    dur_ms = 250
    n = int(SAMPLE_RATE * dur_ms / 1000)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        # Sub-bass rumble
        sub = 0.5 * math.sin(2 * math.pi * 30 * t)
        sub += 0.25 * math.sin(2 * math.pi * 60 * t)
        sub += 0.10 * math.sin(2 * math.pi * 90 * t)
        # Filtered noise texture (low-passed by running average simulation)
        raw_noise = random.random() * 2 - 1
        tex = 0.08 * raw_noise
        # Rhythmic AM pulse
        am = 0.6 + 0.35 * math.sin(2 * math.pi * 4 * t) + 0.05 * math.sin(2 * math.pi * 7.3 * t)
        val = (sub + tex) * am
        samples.append(val * MAX_AMP * 0.65)
    return samples


def gen_yawn():
    """Sleepy yawn: descending vocal chirp that stretches out.
    Uses formant pair for voice-like quality, pitch slides down slowly."""
    dur_ms = 600
    n = int(SAMPLE_RATE * dur_ms / 1000)
    samples = []
    phase1 = 0.0
    phase2 = 0.0
    for i in range(n):
        t = i / n
        # Base pitch descends with a slight arch (opens then closes)
        pitch_curve = 1.0 + 0.08 * math.sin(math.pi * t * 0.8)  # Slight rise then fall
        base_f = (460 - 190 * t) * pitch_curve
        formant_f = 850 - 200 * t  # Formant tracks down too
        # Tiny vibrato (sleepy wobble)
        base_f += 5 * math.sin(2 * math.pi * 3 * (i / SAMPLE_RATE))
        phase1 += base_f / SAMPLE_RATE
        phase2 += formant_f / SAMPLE_RATE
        carrier = math.sin(2 * math.pi * phase1)
        formant = 0.3 * math.sin(2 * math.pi * phase2)
        val = carrier * 0.6 + carrier * formant * 0.4
        # Breathy overlay
        breath = (random.random() * 2 - 1) * 0.05 * (1 - t * 0.6)
        val += breath
        # Gentle envelope
        attack = min(1.0, i / (n * 0.12))
        release = min(1.0, (n - i) / (n * 0.25))
        env = attack * release
        samples.append(val * env * MAX_AMP * 0.55)
    return samples


def gen_yelp_scared():
    """Quick startled 'eep!': sharp upward bubble pop.
    Opposite of normal pop — frequency sweeps UP for surprise."""
    n = int(SAMPLE_RATE * 0.075)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / n
        # Rapid upward sweep — startled!
        freq = 500 + 600 * (1 - math.exp(-8 * t))
        phase += freq / SAMPLE_RATE
        val = math.sin(2 * math.pi * phase)
        fade_in = min(1.0, i / max(int(SAMPLE_RATE * 0.001), 1))
        env = fade_in * math.exp(-4 * t)
        samples.append(val * env * MAX_AMP * 0.8)
    return samples


def gen_sigh_relieved():
    """Gentle exhale: descending vocal with breathy noise layer."""
    dur_ms = 350
    n = int(SAMPLE_RATE * dur_ms / 1000)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / n
        freq = 400 - 130 * t
        phase += freq / SAMPLE_RATE
        tone = math.sin(2 * math.pi * phase) * 0.4
        # Breathy noise (dominant, fading)
        breath = (random.random() * 2 - 1) * 0.1 * (1 - t * 0.7)
        val = tone + breath
        attack = min(1.0, i / (n * 0.1))
        release = min(1.0, (n - i) / (n * 0.3))
        samples.append(val * attack * release * MAX_AMP * 0.5)
    return samples


def gen_grunt_angry():
    """Short grumbly growl: low distorted rumble."""
    dur_ms = 150
    n = int(SAMPLE_RATE * dur_ms / 1000)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / SAMPLE_RATE
        freq = 95 + 15 * math.sin(2 * math.pi * 8 * t)
        phase += freq / SAMPLE_RATE
        val = (0.5 * math.sin(2 * math.pi * phase) +
               0.3 * math.sin(2 * math.pi * 2 * phase) +
               0.12 * math.sin(2 * math.pi * 3 * phase))
        val = math.tanh(val * 1.3)  # Soft distortion
        val += (random.random() * 2 - 1) * 0.05  # Grit
        env = math.exp(-2 * (i / n))
        fade_in = min(1.0, i / max(int(SAMPLE_RATE * 0.005), 1))
        samples.append(val * fade_in * env * MAX_AMP * 0.6)
    return samples


def gen_attention_melody():
    """Three ascending water drops — gentle 'plink plink PLINK!'"""
    drops = [
        (600, 80, 0.5),    # freq, dur_ms, amp
        (780, 75, 0.55),
        (960, 110, 0.65),   # Last one rings
    ]
    samples = []
    for freq, dur, amp in drops:
        samples += water_drop(freq, dur, amplitude=amp)
        samples += silence(45)
    return samples


def gen_lonely_melody():
    """Two slow descending vocal notes — sad and gentle.
    Uses formant pairs for voice-like sadness."""
    notes = [
        (380, 720, 300, 0.4),   # base_f, formant_f, dur_ms, amp
        (310, 640, 380, 0.32),
    ]
    samples = []
    for base_f, form_f, dur_ms, amp in notes:
        n = int(SAMPLE_RATE * dur_ms / 1000)
        phase1 = 0.0
        phase2 = 0.0
        for i in range(n):
            t = i / n
            f1 = base_f - 30 * t  # Slight pitch droop
            f1 += 4 * math.sin(2 * math.pi * 4 * (i / SAMPLE_RATE))  # Sad vibrato
            f2 = form_f - 40 * t
            phase1 += f1 / SAMPLE_RATE
            phase2 += f2 / SAMPLE_RATE
            carrier = math.sin(2 * math.pi * phase1)
            formant = 0.3 * math.sin(2 * math.pi * phase2)
            val = carrier * 0.6 + carrier * formant * 0.4
            attack = min(1.0, i / (n * 0.12))
            release = min(1.0, (n - i) / (n * 0.3))
            samples.append(val * attack * release * amp * MAX_AMP)
        samples += silence(70)
    return samples


def gen_good_morning():
    """Bright ascending water drops — cheerful wake-up plinks."""
    drops = [
        (550, 70, 0.50),
        (700, 65, 0.55),
        (850, 65, 0.60),
        (1000, 95, 0.65),
    ]
    samples = []
    for freq, dur, amp in drops:
        samples += water_drop(freq, dur, amplitude=amp)
        samples += silence(25)
    return samples


def gen_good_night():
    """Descending gentle water drops — soothing lullaby plinks."""
    drops = [
        (800, 100, 0.40),
        (650, 110, 0.35),
        (500, 140, 0.28),
    ]
    samples = []
    for freq, dur, amp in drops:
        samples += water_drop(freq, dur, amplitude=amp)
        samples += silence(55)
    return samples


def gen_dizzy():
    """Wobbly bubble — frequency oscillates like spinning."""
    dur_ms = 300
    n = int(SAMPLE_RATE * dur_ms / 1000)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / n
        t_sec = i / SAMPLE_RATE
        # Wide wobble
        wobble = 100 * math.sin(2 * math.pi * 4.5 * t_sec)
        freq = 580 + wobble
        phase += freq / SAMPLE_RATE
        val = math.sin(2 * math.pi * phase) + 0.08 * math.sin(2 * math.pi * 2 * phase)
        attack = min(1.0, i / (n * 0.06))
        release = min(1.0, (n - i) / (n * 0.2))
        samples.append(val * attack * release * MAX_AMP * 0.55)
    return samples


def gen_joy_burst():
    """Rapid ascending bubble pops — pure bubbly excitement!"""
    pops = [
        (650, 420, 35, 0.6),
        (800, 500, 32, 0.65),
        (950, 580, 30, 0.70),
        (1100, 660, 28, 0.75),
        (1250, 740, 50, 0.80),
    ]
    samples = []
    for fs, fe, dur, amp in pops:
        samples += bubble_pop(fs, fe, dur, amplitude=amp, decay_speed=5.5)
        samples += silence(6)
    return samples


def gen_confirm():
    """Soft acknowledgment pip: single clean water drop.
    Quick, subtle, non-intrusive."""
    return water_drop(820, 80, amplitude=0.45)


# =============================================================================
# Main
# =============================================================================

def main():
    output_dir = os.path.join(os.path.dirname(__file__), '..', 'spiffs_assets', 'sounds')
    os.makedirs(output_dir, exist_ok=True)
    print("Generating Mochi PCM sounds (16kHz, 16-bit LE, mono):\n")

    save_pcm('chirp_happy.pcm', gen_chirp_happy(), output_dir)
    save_pcm('chirp_excited.pcm', gen_chirp_excited(), output_dir)
    save_pcm('purr_loop.pcm', gen_purr_loop(), output_dir)
    save_pcm('yawn.pcm', gen_yawn(), output_dir)
    save_pcm('yelp_scared.pcm', gen_yelp_scared(), output_dir)
    save_pcm('sigh_relieved.pcm', gen_sigh_relieved(), output_dir)
    save_pcm('grunt_angry.pcm', gen_grunt_angry(), output_dir)
    save_pcm('attention_melody.pcm', gen_attention_melody(), output_dir)
    save_pcm('lonely_melody.pcm', gen_lonely_melody(), output_dir)
    save_pcm('good_morning.pcm', gen_good_morning(), output_dir)
    save_pcm('good_night.pcm', gen_good_night(), output_dir)
    save_pcm('dizzy.pcm', gen_dizzy(), output_dir)
    save_pcm('joy_burst.pcm', gen_joy_burst(), output_dir)
    save_pcm('confirm.pcm', gen_confirm(), output_dir)

    print(f"\nAll 14 PCM files generated in {os.path.abspath(output_dir)}")


if __name__ == '__main__':
    main()
