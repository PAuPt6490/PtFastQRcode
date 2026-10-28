# -*- coding: utf-8 -*-
"""LTCG refuses objects that define the compiler's library helpers, so crt_mem.c is
compiled separately without /GL."""
import io

path = r"C:\PtFastQRcode\build.ps1"
raw = io.open(path, "rb").read()
text = raw.decode("utf-8-sig")

old = """$compileArgs = @(
    '/nologo', '/c', '/O2', '/GL', '/MT', '/NODEFAULTLIB', '/GS-', '/EHs-c-', '/GR-',
    '/D_HAS_EXCEPTIONS=0', '/utf-8', '/W3',
    '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00',
    "/I$(Join-Path $root 'src')", "/I$(Join-Path $root 'src\\res')",
    "/Fo$objDir\\"
) + $IncFlags + $sources

& $ClExe @compileArgs
if ($LASTEXITCODE -ne 0) { throw "cl.exe failed with $LASTEXITCODE" }
"""
new = """$commonDefines = @(
    '/nologo', '/c', '/O2', '/MT', '/NODEFAULTLIB', '/GS-', '/EHs-c-', '/GR-',
    '/D_HAS_EXCEPTIONS=0', '/utf-8', '/W3',
    '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00',
    "/I$(Join-Path $root 'src')", "/I$(Join-Path $root 'src\\res')",
    "/Fo$objDir\\"
) + $IncFlags

# Everything but the CRT shim goes through LTCG.
$ltcgSources = $sources | Where-Object { $_ -notlike '*crt_mem.c' }
& $ClExe @commonDefines @('/GL') $ltcgSources
if ($LASTEXITCODE -ne 0) { throw "cl.exe failed with $LASTEXITCODE" }

# crt_mem.c defines the compiler's library helpers, which /GL cannot carry.
& $ClExe @commonDefines (Join-Path $root 'src\\app\\crt_mem.c')
if ($LASTEXITCODE -ne 0) { throw "cl.exe failed on crt_mem.c with $LASTEXITCODE" }
"""
assert old in text, "compile block"
text = text.replace(old, new, 1)

# /LTCG is fine at link time; mixed objects are allowed.
io.open(path, "wb").write(b"\xef\xbb\xbf" + text.encode("utf-8"))
print("build.ps1: crt_mem.c compiled without /GL")
