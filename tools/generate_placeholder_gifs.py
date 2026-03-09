#!/usr/bin/env python3
"""Generate 15 placeholder GIFs for Mochi emotion testing.

Each GIF is 150x150, animated (2 frames), with a distinct background color
and the emotion name rendered as text. Frame delay is 500ms.

Output: spiffs_assets/gifs/<emotion_name>.gif
"""

import os
import sys
from PIL import Image, ImageDraw, ImageFont

# Emotion → hex color mapping (for easy visual debugging on device)
EMOTIONS = {
    "idle":      "#4A90D9",   # blue
    "listening": "#00E5FF",   # cyan
    "thinking":  "#9B59B6",   # purple
    "talking":   "#2ECC71",   # green
    "happy":     "#F1C40F",   # yellow
    "excited":   "#FF6B35",   # orange
    "loved":     "#FF69B4",   # pink
    "sad":       "#5D8AA8",   # steel blue
    "angry":     "#E74C3C",   # red
    "scared":    "#ECEEEF",   # light gray
    "startled":  "#FF4500",   # red-orange
    "relieved":  "#A8D8A8",   # sage green
    "sleepy":    "#2C3E50",   # dark navy
    "confused":  "#F39C12",   # amber
    "bored":     "#808080",   # gray
}

GIF_SIZE = (150, 150)
FRAME_DELAY_MS = 500


def hex_to_rgb(hex_color):
    h = hex_color.lstrip("#")
    return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))


def contrast_color(rgb):
    """Return black or white text depending on background brightness."""
    brightness = (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) / 1000
    return (0, 0, 0) if brightness > 128 else (255, 255, 255)


def create_frame(emotion_name, bg_rgb, text_color, frame_num):
    """Create a single GIF frame."""
    img = Image.new("RGB", GIF_SIZE, bg_rgb)
    draw = ImageDraw.Draw(img)

    # Draw emotion name centered
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 16)
    except (IOError, OSError):
        font = ImageFont.load_default()

    # Get text bounding box for centering
    bbox = draw.textbbox((0, 0), emotion_name, font=font)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]
    x = (GIF_SIZE[0] - text_w) // 2
    y = (GIF_SIZE[1] - text_h) // 2

    draw.text((x, y), emotion_name, fill=text_color, font=font)

    # Frame 2: add a small dot indicator to prove animation
    if frame_num == 1:
        draw.ellipse([65, 120, 85, 140], fill=text_color)

    return img


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    output_dir = os.path.join(project_root, "spiffs_assets", "gifs")
    os.makedirs(output_dir, exist_ok=True)

    print(f"Generating {len(EMOTIONS)} placeholder GIFs to: {output_dir}")

    for emotion_name, hex_color in EMOTIONS.items():
        bg_rgb = hex_to_rgb(hex_color)
        text_color = contrast_color(bg_rgb)

        frame0 = create_frame(emotion_name, bg_rgb, text_color, 0)
        frame1 = create_frame(emotion_name, bg_rgb, text_color, 1)

        gif_path = os.path.join(output_dir, f"{emotion_name}.gif")
        frame0.save(
            gif_path,
            save_all=True,
            append_images=[frame1],
            duration=FRAME_DELAY_MS,
            loop=0,  # infinite loop
        )

        file_size = os.path.getsize(gif_path)
        print(f"  {emotion_name:12s} -> {gif_path} ({file_size} bytes)")

    print(f"\nDone. {len(EMOTIONS)} GIFs generated.")


if __name__ == "__main__":
    main()
