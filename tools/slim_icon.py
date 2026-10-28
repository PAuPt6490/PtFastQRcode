# -*- coding: utf-8 -*-
"""Rebuild app.ico small: 32bpp BMP entries for shell sizes, one paletted PNG for 256.

The artwork is two colours plus antialiasing, so a 32-colour palette is visually
lossless and shrinks the 256px entry from ~30 KB to a few KB. ICO payloads are written
by hand because Pillow always emits every size.
"""
import io
import os
import struct

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "PtFastQRcode.png")
OUT = os.path.join(ROOT, "assets", "app.ico")

SHELL_SIZES = [16, 20, 24, 32, 48]
LARGE = 256

src = Image.open(SRC).convert("RGBA").resize((1024, 1024), Image.LANCZOS)


def bmp_dib(image):
    """32bpp ICO bitmap payload: BITMAPINFOHEADER + BGRA rows + 1bpp AND mask."""
    size = image.size[0]
    pixels = image.load()
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0)

    xor = bytearray()
    for y in range(size - 1, -1, -1):          # bottom-up
        for x in range(size):
            r, g, b, a = pixels[x, y]
            xor += bytes((b, g, r, a))

    mask_row = ((size + 31) // 32) * 4         # 1bpp, rows padded to 4 bytes
    and_mask = bytes(mask_row * size)          # all zero: shape comes from alpha

    return header + bytes(xor) + and_mask


def build(path):
    entries = []
    for size in SHELL_SIZES:
        frame = src.resize((size, size), Image.LANCZOS)
        entries.append((size, bmp_dib(frame)))

    big = (src.resize((LARGE, LARGE), Image.LANCZOS).convert("RGB")
           .quantize(colors=32, method=Image.MEDIANCUT, dither=Image.NONE))
    png = io.BytesIO()
    big.save(png, format="PNG", optimize=True)   # mode P -> palette PNG, a few KB
    entries.append((LARGE, png.getvalue()))

    out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    directory = bytearray()
    payload = bytearray()
    for size, blob in entries:
        dim = 0 if size >= 256 else size
        directory += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset)
        payload += blob
        offset += len(blob)

    with open(path, "wb") as f:
        f.write(out + directory + payload)


build(OUT)
print("app.ico: %d bytes" % os.path.getsize(OUT))
src.resize((256, 256), Image.LANCZOS).save(os.path.join(ROOT, "assets", "app-256.png"))
print("app-256.png: %d bytes" % os.path.getsize(os.path.join(ROOT, "assets", "app-256.png")))
