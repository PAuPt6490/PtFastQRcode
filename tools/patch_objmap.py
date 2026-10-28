# -*- coding: utf-8 -*-
"""Object-name mapping must handle .c sources too."""
import io

path = r"C:\PtFastQRcode\build.ps1"
raw = io.open(path, "rb").read()
text = raw.decode("utf-8-sig")

old = """$objects = $sources | ForEach-Object {
    Join-Path $objDir ((Split-Path -Leaf $_).Replace('.cpp', '.obj'))
}"""
new = """$objects = $sources | ForEach-Object {
    $leaf = (Split-Path -Leaf $_).Replace('.cpp', '.obj').Replace('.c', '.obj')
    Join-Path $objDir $leaf
}"""
assert old in text, "objects mapping"
text = text.replace(old, new, 1)
io.open(path, "wb").write(b"\xef\xbb\xbf" + text.encode("utf-8"))
print("object mapping fixed")
