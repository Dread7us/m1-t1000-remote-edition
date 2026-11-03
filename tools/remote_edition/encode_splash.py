#!/usr/bin/env python3
"""Encode the approved emblem as an M1 48x38 monochrome XBM byte array."""
from pathlib import Path
import argparse
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
args = parser.parse_args()
source = Image.open(args.source).convert("L")
source = source.point(lambda pixel: 0 if pixel < 128 else 255)
bounds = ImageOps.invert(source).getbbox()
if not bounds: raise SystemExit("The source is empty.")
source = source.crop(bounds)
source.thumbnail((46, 36), Image.Resampling.NEAREST)
canvas = Image.new("L", (48, 38), 255)
canvas.paste(source, ((48-source.width)//2, (38-source.height)//2))
destination = ROOT / "documentation/remote-edition"
canvas.save(destination / "avatar-emblem-48x38.png")
data = []
for y in range(38):
    for block in range(6):
        data.append(sum((1 << bit) for bit in range(8) if canvas.getpixel((block*8+bit, y)) < 128))
header = """/* Monochrome emblem adapted from Dread7us's GitHub avatar. */
#ifndef M1_SPLASH_ART_H_
#define M1_SPLASH_ART_H_
#include <stdint.h>
#define M1_SPLASH_ART_WIDTH 48
#define M1_SPLASH_ART_HEIGHT 38
static const uint8_t m1_splash_avatar_48x38[] = {
"""
for offset in range(0, len(data), 12):
    header += "    " + ", ".join(f"0x{value:02x}" for value in data[offset:offset+12]) + ",\n"
header += "};\n#endif\n"
(ROOT / "m1_csrc/m1_splash_art.h").write_text(header, encoding="utf-8", newline="\n")
print("Encoded 228 bytes; preview and header saved in the project.")
