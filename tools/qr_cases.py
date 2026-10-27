# -*- coding: utf-8 -*-
"""Generate the corpus of test payloads for QR encoder verification."""
import base64
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tools", "qr_cases.txt")

cases = [
    "",
    "A",
    "a",
    "0",
    "0123456789",
    "012345678901234567890123456789",
    "HELLO WORLD",
    "HELLO WORLD 123",
    "HTTPS://EXAMPLE.COM/A/B?C=D",
    "https://github.com/",
    "https://example.com/path?query=1&x=2#frag",
    "中文测试:快捷二维码",
    "こんにちは世界",
    "\U0001F600\U0001F389 emoji test",
    "\t\n special \r\n chars \x00nul",
    "WIFI:T:WPA;S:MyNet;P:p@ssw0rd!;;",
    "mailto:someone@example.com?subject=Hi",
    "tel:+8613800138000",
    "a" * 200,
    "a" * 500,
    "a" * 1000,
    "a" * 2000,
    "a" * 2950,
    "a" * 3000,
    "9" * 500,
    "9" * 3000,
    "ABC 123 $%*+-./:" * 12,
    "x" * 100 + "中文" * 100,
    "http://例子.测试/路径/文件?参数=值",
]

with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    for c in cases:
        # An empty line is skipped by the harness; "-" is its sentinel for the empty payload.
        f.write("-" if c == "" else base64.b64encode(c.encode("utf-8")).decode("ascii"))
        f.write("\n")

with open(os.path.join(ROOT, "tools", "qr_cases.json"), "w", encoding="utf-8") as f:
    json.dump(cases, f, ensure_ascii=False, indent=1)

print("cases:", len(cases), "->", OUT)
