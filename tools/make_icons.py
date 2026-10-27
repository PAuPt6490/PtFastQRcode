# -*- coding: utf-8 -*-
"""Build Windows multi-size .ico files from the supplied artwork."""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "PtFastQRcode.png")
ASSETS = os.path.join(ROOT, "assets")
os.makedirs(ASSETS, exist_ok=True)

img = Image.open(SRC).convert("RGBA")
img = img.resize((1024, 1024), Image.LANCZOS)

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]

# Windows accepts up to 256px inside an .ico; app icon carries every size so the
# shell, taskbar, alt-tab and the tray can each pick a native frame.
app_ico = os.path.join(ASSETS, "app.ico")
img.save(app_ico, format="ICO", sizes=[(s, s) for s in SIZES])
print("app.ico", os.path.getsize(app_ico), "bytes")

# Preview sheet: how the artwork reads at tray sizes (16/20/24 px, nearest-neighbour
# upscaled 8x so we can judge legibility of tiny frames).
sheet = Image.new("RGBA", (16 + 20 + 24 + 3 * 12, 24), (255, 255, 255, 255))
x = 0
for s in (16, 20, 24):
    small = img.resize((s, s), Image.LANCZOS)
    sheet.alpha_composite(small, (x, 0))
    x += s + 12
sheet = sheet.resize((sheet.width * 8, sheet.height * 8), Image.NEAREST)
sheet.save(os.path.join(ROOT, "tools", "icon-preview-16-20-24.png"))

# Full-size preview downscaled to 256 for the README / window header.
img.resize((256, 256), Image.LANCZOS).save(os.path.join(ASSETS, "app-256.png"))
print("done")
