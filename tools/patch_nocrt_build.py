# -*- coding: utf-8 -*-
"""Add the wmem* primitives std::wstring needs, and switch the build to a CRT-free link."""
import io

# ---------------------------------------------------------------- crt_stub.cpp
path = r"C:\PtFastQRcode\src\app\crt_stub.cpp"
text = io.open(path, encoding="utf-8").read()
anchor = 'extern "C" int __cdecl _wtoi(const wchar_t* text) {'
addition = '''extern "C" wchar_t* __cdecl wmemcpy(wchar_t* destination, const wchar_t* source, size_t count) {
    __movsw((unsigned short*)destination, (const unsigned short*)source, count);
    return destination;
}

#pragma optimize("", off)
extern "C" wchar_t* __cdecl wmemmove(wchar_t* destination, const wchar_t* source, size_t count) {
    if (destination == source || count == 0) return destination;
    if (destination < source || destination >= source + count) {
        for (size_t i = 0; i < count; ++i) destination[i] = source[i];
    } else {
        for (size_t i = count; i > 0; --i) destination[i - 1] = source[i - 1];
    }
    return destination;
}
#pragma optimize("", on)

extern "C" wchar_t* __cdecl wmemset(wchar_t* destination, wchar_t value, size_t count) {
    __stosw((unsigned short*)destination, (unsigned short)value, count);
    return destination;
}

extern "C" int __cdecl wmemcmp(const wchar_t* left, const wchar_t* right, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (left[i] != right[i]) return (int)left[i] - (int)right[i];
    }
    return 0;
}

'''
assert anchor in text
text = text.replace(anchor, addition + anchor, 1)
io.open(path, "w", encoding="utf-8", newline="\n").write(text)
print("crt_stub.cpp: added wmem*")

# ---------------------------------------------------------------- build.ps1
path = r"C:\PtFastQRcode\build.ps1"
text = io.open(path, encoding="utf-8-sig").read()

old_sources = """$sources = @(
    'src\\qr\\qrcode.cpp',
    'src\\app\\core.cpp',
    'src\\app\\ui.cpp',
    'src\\app\\qrimage.cpp',
    'src\\app\\popup.cpp',
    'src\\app\\settings_win.cpp',
    'src\\app\\main.cpp'
)"""
new_sources = """$sources = @(
    'src\\qr\\qrcode.cpp',
    'src\\app\\core.cpp',
    'src\\app\\ui.cpp',
    'src\\app\\qrimage.cpp',
    'src\\app\\popup.cpp',
    'src\\app\\settings_win.cpp',
    'src\\app\\main.cpp',
    'src\\app\\text_util.cpp',
    'src\\app\\crt_stub.cpp'
)"""
assert old_sources in text, "sources"
text = text.replace(old_sources, new_sources, 1)

old_flags = """$compileArgs = @(
    '/nologo', '/c', '/O2', '/GL', '/MT', '/EHsc', '/utf-8', '/W3',
    '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00',"""
new_flags = """# No C runtime: crt_stub.cpp provides the entry point, allocator and memory primitives.
# Exceptions and RTTI are off so the C++ EH runtime is never referenced.
$compileArgs = @(
    '/nologo', '/c', '/O2', '/GL', '/MT', '/NODEFAULTLIB', '/GS-', '/EHs-c-', '/GR-',
    '/D_HAS_EXCEPTIONS=0', '/utf-8', '/W3',
    '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00',"""
assert old_flags in text, "flags"
text = text.replace(old_flags, new_flags, 1)

old_link = """$linkArgs = @(
    '/nologo', '/SUBSYSTEM:WINDOWS', '/LTCG', '/OPT:REF', '/OPT:ICF', '/MANIFEST:NO',
    "/OUT:$exePath\""""
new_link = """$linkArgs = @(
    '/nologo', '/SUBSYSTEM:WINDOWS', '/LTCG', '/OPT:REF', '/OPT:ICF', '/MANIFEST:NO',
    '/NODEFAULTLIB', '/ENTRY:entryPoint',
    "/OUT:$exePath\""""
assert old_link in text, "link"
text = text.replace(old_link, new_link, 1)

io.open(path, "wb").write(b"\xef\xbb\xbf" + text.encode("utf-8"))
print("build.ps1: CRT-free flags")
