# -*- coding: utf-8 -*-
"""With /NODEFAULTLIB every import library must be listed explicitly (kernel32/uxtheme
used to arrive via the static CRT's default-lib directives)."""
import io

path = r"C:\PtFastQRcode\build.ps1"
raw = io.open(path, "rb").read()
text = raw.decode("utf-8-sig")

old = """    'user32.lib', 'gdi32.lib', 'gdiplus.lib', 'shell32.lib', 'shlwapi.lib',
    'ole32.lib', 'comctl32.lib', 'comdlg32.lib', 'uuid.lib', 'advapi32.lib'
)"""
new = """    'kernel32.lib', 'user32.lib', 'gdi32.lib', 'gdiplus.lib', 'shell32.lib',
    'shlwapi.lib', 'ole32.lib', 'comctl32.lib', 'comdlg32.lib', 'uuid.lib',
    'advapi32.lib', 'uxtheme.lib'
)"""
assert old in text, "libs"
text = text.replace(old, new, 1)
io.open(path, "wb").write(b"\xef\xbb\xbf" + text.encode("utf-8"))
print("link libraries completed")
