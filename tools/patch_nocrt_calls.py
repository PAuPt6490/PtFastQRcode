# -*- coding: utf-8 -*-
"""Swap CRT/STL string calls for the local helpers so no libcmt/libcpmt is needed."""
import io

ROOT = r"C:\PtFastQRcode\src\app"


def patch(name, pairs):
    path = ROOT + "\\" + name
    text = io.open(path, encoding="utf-8").read()
    for old, new in pairs:
        if old not in text:
            raise SystemExit("MISSING in %s: %s" % (name, old[:60]))
        text = text.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="\n").write(text)
    print("patched", name)


patch("core.cpp", [
    ('#include "app.h"', '#include "app.h"\n\n#include "text_util.h"'),
    ("if (_wcsicmp(face, name) == 0) {", "if (tu::compareNoCase(face, name) == 0) {"),
])

patch("main.cpp", [
    ('#include "app.h"', '#include "app.h"\n\n#include "text_util.h"'),
    ("wcsncpy_s(data.szTip, tooltip.c_str(), _TRUNCATE);",
     "tu::copy(data.szTip, ARRAYSIZE(data.szTip), tooltip.c_str());"),
    ('        std::wstring message = L"重启失败,请手动退出后重新打开。错误码:" +\n'
     '                               std::to_wstring(GetLastError());',
     '        wchar_t code[32];\n'
     '        tu::formatUnsigned(code, ARRAYSIZE(code), (unsigned long)GetLastError());\n'
     '        std::wstring message = L"重启失败,请手动退出后重新打开。错误码:" + std::wstring(code);'),
    ('    bool startHidden = arguments.find(L"--startup") != std::wstring::npos;\n'
     '    bool requestShow = arguments.find(L"--show") != std::wstring::npos;\n'
     '    bool restartMode = arguments.find(L"--restart") != std::wstring::npos;',
     '    bool startHidden = tu::contains(arguments.c_str(), L"--startup");\n'
     '    bool requestShow = tu::contains(arguments.c_str(), L"--show");\n'
     '    bool restartMode = tu::contains(arguments.c_str(), L"--restart");'),
])

patch("popup.cpp", [
    ('#include "app.h"', '#include "app.h"\n\n#include "text_util.h"'),
    ('    size_t slash = path.find_last_of(L"\\\\/");',
     '    size_t slash = tu::lastPathSeparator(path);'),
    ("    wcsncpy_s(buffer.data(), buffer.size(), suggested.c_str(), _TRUNCATE);",
     "    tu::copy(buffer.data(), buffer.size(), suggested.c_str());"),
])

patch("qrimage.cpp", [
    ('#include "app.h"', '#include "app.h"\n\n#include "text_util.h"'),
    ('    ensureDirectory(path.substr(0, path.find_last_of(L\'\\\\\')));',
     '    const std::wstring::size_type slash = tu::lastPathSeparator(path);\n'
     '    if (slash != std::wstring::npos) ensureDirectory(path.substr(0, slash));'),
    ('        bool invalid = wcschr(L"\\\\/:*?\\"<>|", c) != nullptr;',
     '        bool invalid = tu::containsChar(L"\\\\/:*?\\"<>|", c);'),
])

patch("settings_win.cpp", [
    ('#include "app.h"', '#include "app.h"\n\n#include "text_util.h"'),
    ("    int size = _wtoi(sizeText);", "    int size = tu::toInt(sizeText);"),
    ("                int value = _wtoi(text);", "                int value = tu::toInt(text);"),
])

# text_util: add the character-set helper
path = ROOT + "\\text_util.h"
text = io.open(path, encoding="utf-8").read()
text = text.replace("// Bounded copy, always NUL-terminated (like wcsncpy_s with _TRUNCATE).",
                    "// Tests whether a character belongs to a literal set.\n"
                    "bool containsChar(const wchar_t* set, wchar_t value);\n\n"
                    "// Bounded copy, always NUL-terminated (like wcsncpy_s with _TRUNCATE).")
io.open(path, "w", encoding="utf-8", newline="\n").write(text)

path = ROOT + "\\text_util.cpp"
text = io.open(path, encoding="utf-8").read()
text = text.replace("void copy(wchar_t* destination, std::size_t capacity, const wchar_t* source) {",
                    "bool containsChar(const wchar_t* set, wchar_t value) {\n"
                    "    if (!set) return false;\n"
                    "    for (const wchar_t* cursor = set; *cursor; ++cursor) {\n"
                    "        if (*cursor == value) return true;\n"
                    "    }\n"
                    "    return false;\n"
                    "}\n\n"
                    "void copy(wchar_t* destination, std::size_t capacity, const wchar_t* source) {")
io.open(path, "w", encoding="utf-8", newline="\n").write(text)
print("text_util extended")
