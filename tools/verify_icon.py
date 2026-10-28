# -*- coding: utf-8 -*-
"""Checks that every frame of assets/app.ico decodes and dumps the 256px frame."""
import os
from PIL import Image

ROOT = r"C:\PtFastQRcode"
path = os.path.join(ROOT, "assets", "app.ico")
im = Image.open(path)
sizes = sorted(im.info["sizes"])
print("icon bytes:", os.path.getsize(path))
print("frames:", sizes)
for size in sizes:
    im.size = size
    im.load()
    print("  ", size, im.mode)
    if size == (256, 256):
        im.save(os.path.join(ROOT, "tools", "icon256-check.png"))
