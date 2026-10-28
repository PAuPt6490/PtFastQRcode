# -*- coding: utf-8 -*-
"""RECT helpers belong with windows.h, not in the text utility header."""
import io

# text_util: drop the overlaps declaration/definition
path = r"C:\PtFastQRcode\src\app\text_util.h"
text = io.open(path, encoding="utf-8-sig").read()
text = text.replace("// Rectangle intersection test (both in the same coordinate space).\n"
                    "bool overlaps(const RECT& a, const RECT& b);\n\n", "")
io.open(path, "w", encoding="utf-8", newline="\n").write(text)

path = r"C:\PtFastQRcode\src\app\text_util.cpp"
text = io.open(path, encoding="utf-8-sig").read()
text = text.replace("bool overlaps(const RECT& a, const RECT& b) {\n"
                    "    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;\n"
                    "}\n\n", "")
io.open(path, "w", encoding="utf-8", newline="\n").write(text)

# app.h: inline helper next to the drawing declarations
path = r"C:\PtFastQRcode\src\app\app.h"
text = io.open(path, encoding="utf-8-sig").read()
old = "// Rounded rectangle drawn with GDI. CLR_NONE as fill = hollow, as stroke = no border."
new = ("// Two rectangles in the same coordinate space.\n"
       "inline bool rectsOverlap(const RECT& a, const RECT& b) {\n"
       "    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;\n"
       "}\n\n"
       "// Rounded rectangle drawn with GDI. CLR_NONE as fill = hollow, as stroke = no border.")
assert old in text
text = text.replace(old, new, 1)
io.open(path, "w", encoding="utf-8", newline="\n").write(text)

# popup.cpp: use the header helper
path = r"C:\PtFastQRcode\src\app\popup.cpp"
text = io.open(path, encoding="utf-8-sig").read()
text = text.replace("tu::overlaps(", "rectsOverlap(")
io.open(path, "w", encoding="utf-8", newline="\n").write(text)
print("rect helper moved to app.h")
