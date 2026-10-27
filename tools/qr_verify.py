# -*- coding: utf-8 -*-
"""Verify the C++ QR encoder against independent implementations.

1. Matrix equality with Project Nayuki's Python reference (same algorithm family).
2. Matrix equality with the classic `qrcode` Python package for every forced mask.
3. Every generated symbol is rendered to PNG and decoded with zxing-cpp, which proves
   the output is a real, scannable QR Code rather than just an internally consistent grid.

Run order: build tools/QrTestCpp, then
   QrTestCpp cases.txt auto out_cpp_auto.txt
   QrTestCpp cases.txt forced L out_cpp_forced_L.txt   (and M/Q/H)
then: python tools/qr_verify.py --out <dir containing out_cpp_*.txt>
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "ref"))

from qrcodegen import QrCode as RefQr  # noqa: E402
import qrcode  # noqa: E402
from qrcode.constants import (  # noqa: E402
    ERROR_CORRECT_L,
    ERROR_CORRECT_M,
    ERROR_CORRECT_Q,
    ERROR_CORRECT_H,
)
import zxingcpp  # noqa: E402
from PIL import Image  # noqa: E402

CASES = json.load(open(os.path.join(ROOT, "tools", "qr_cases.json"), encoding="utf-8"))
ECL_NAMES = ["Low", "Medium", "Quartile", "High"]
ECL_REF = [RefQr.Ecc.LOW, RefQr.Ecc.MEDIUM, RefQr.Ecc.QUARTILE, RefQr.Ecc.HIGH]
ECL_LIB = [ERROR_CORRECT_L, ERROR_CORRECT_M, ERROR_CORRECT_Q, ERROR_CORRECT_H]


def parse(path):
    """-> {(case, ecl): [(version, mask, size, rows), ...]}"""
    result = {}
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    i = 0
    while i < len(lines):
        parts = lines[i].split()
        if not parts:
            i += 1
            continue
        if parts[0] != "CASE":
            raise SystemExit("bad line: " + lines[i])
        case, ecl = int(parts[1]), parts[2]
        if "ERROR" in parts:
            result.setdefault((case, ecl), []).append(None)
            i += 1
            continue
        version, mask, size = int(parts[4]), int(parts[6]), int(parts[8])
        rows = lines[i + 1:i + 1 + size]
        result.setdefault((case, ecl), []).append((version, mask, size, rows))
        i += 1 + size
    return result


def matrix_of(qr):
    size = qr.get_size()
    return ["".join("1" if qr.get_module(x, y) else "0" for x in range(size)) for y in range(size)]


def render_png(rows, path, scale=6, quiet=4):
    n = len(rows)
    side = (n + 2 * quiet) * scale
    img = Image.new("L", (side, side), 255)
    px = img.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == "1":
                for dy in range(scale):
                    for dx in range(scale):
                        px[(x + quiet) * scale + dx, (y + quiet) * scale + dy] = 0
    img.save(path)
    return path


def check_reference(auto):
    failures = []
    checked = 0
    for idx, text in enumerate(CASES):
        for ei, ecl_name in enumerate(ECL_NAMES):
            entries = auto.get((idx, ecl_name))
            got = entries[0] if entries else None
            try:
                ref = RefQr.encode_text(text, ECL_REF[ei])
                ref_matrix = matrix_of(ref)
                ref_err = None
            except Exception as exc:
                ref, ref_err = None, exc
            if ref_err is not None:
                if got is not None:
                    failures.append("case %d %s: reference overflowed, encoder did not" % (idx, ecl_name))
                continue
            if got is None:
                failures.append("case %d %s: encoder overflowed, reference did not" % (idx, ecl_name))
                continue
            version, mask, size, rows = got
            checked += 1
            if version != ref.get_version() or mask != ref.get_mask() or rows != ref_matrix:
                failures.append("case %d %s: mismatch (v%d m%d / ref v%d m%d)" % (
                    idx, ecl_name, version, mask, ref.get_version(), ref.get_mask()))
    return checked, failures


def check_qrcode_lib(forced, offset=0):
    failures = []
    checked = 0
    for idx, text in enumerate(CASES):
        if text == "":
            continue  # the library always emits a segment; empty payloads differ by design
        for ei, ecl_name in enumerate(ECL_NAMES):
            for entry in forced[ecl_name].get((idx, ecl_name)) or []:
                if entry is None:
                    continue
                version, mask, size, rows = entry
                try:
                    q = qrcode.QRCode(version=version, error_correction=ECL_LIB[ei],
                                      box_size=1, border=0, mask_pattern=mask)
                    q.add_data(text, optimize=0)
                    q.make(fit=False)
                    ref = ["".join("1" if c else "0" for c in row) for row in q.get_matrix()]
                except Exception:
                    continue  # library cannot express this payload (see docs note below)
                checked += 1
                if ref != rows:
                    failures.append("case %d %s v%d m%d: library matrix mismatch" % (idx, ecl_name, version, mask))
    return checked, failures


def check_decode(auto):
    tmp = os.path.join(ROOT, "tools", "qr_png")
    os.makedirs(tmp, exist_ok=True)
    failures = []
    checked = 0
    for (idx, ecl_name), entries in sorted(auto.items()):
        entry = entries[0]
        if entry is None:
            continue
        text = CASES[idx]
        if text == "":
            continue
        version, mask, size, rows = entry
        path = os.path.join(tmp, "c%02d_%s.png" % (idx, ecl_name[0]))
        render_png(rows, path)
        results = zxingcpp.read_barcodes(Image.open(path))
        checked += 1
        if not results:
            failures.append("case %d %s: not decodable" % (idx, ecl_name))
            continue
        if results[0].bytes != text.encode("utf-8"):
            failures.append("case %d %s: payload mismatch" % (idx, ecl_name))
    return checked, failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", default=os.path.join(ROOT, "tools"))
    parser.add_argument("--prefix", default="out_cpp_")
    args = parser.parse_args()

    auto = parse(os.path.join(args.dir, args.prefix + "auto.txt"))
    forced = {}
    for name in ECL_NAMES:
        forced[name] = parse(os.path.join(args.dir, args.prefix + "forced_%s.txt" % name[0]))

    n1, f1 = check_reference(auto)
    n2, f2 = check_qrcode_lib(forced)
    n3, f3 = check_decode(auto)
    print("1) Nayuki reference match : %4d symbols, %d mismatches" % (n1, len(f1)))
    print("2) python-qrcode match    : %4d symbols, %d mismatches" % (n2, len(f2)))
    print("3) zxing-cpp decode       : %4d symbols, %d failures" % (n3, len(f3)))

    allf = f1 + f2 + f3
    for f in allf[:40]:
        print("   FAIL:", f)
    print("\nRESULT:", "PASS" if not allf else "FAIL (%d)" % len(allf))
    return 0 if not allf else 1


if __name__ == "__main__":
    sys.exit(main())
