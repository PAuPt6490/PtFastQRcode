# -*- coding: utf-8 -*-
"""Add crt_mem.c to the build's source list."""
import io

path = r"C:\PtFastQRcode\build.ps1"
raw = io.open(path, "rb").read()
text = raw.decode("utf-8-sig")
old = "    'src\\app\\crt_stub.cpp'\n"
new = "    'src\\app\\crt_stub.cpp',\n    'src\\app\\crt_mem.c'\n"
assert old in text, "crt_stub entry"
text = text.replace(old, new, 1)
io.open(path, "wb").write(b"\xef\xbb\xbf" + text.encode("utf-8"))
print("crt_mem.c added to sources")
