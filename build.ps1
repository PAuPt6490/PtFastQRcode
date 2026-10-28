# Builds PtFastQRcode as a single statically linked Win32 executable.
# Toolchain: MSVC + Windows SDK already present on this machine (see tools\env.ps1).
#
#   powershell -File build.ps1                 # x64 release build into .\dist
#   powershell -File build.ps1 -Arch x86       # 32-bit build (Win7+ compatible)
#   powershell -File build.ps1 -Clean          # wipe intermediate objects first

param(
    [switch]$Clean,
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

. (Join-Path $root 'tools\env.ps1') -Arch $Arch

$objDir = Join-Path $root "build\obj-$Arch"
$outDir = Join-Path $root 'dist'
$exeName = if ($Arch -eq 'x86') { 'PtFastQRcode-x86.exe' } else { 'PtFastQRcode.exe' }
$exePath = Join-Path $outDir $exeName

if ($Clean -and (Test-Path (Join-Path $root 'build'))) {
    Remove-Item -Recurse -Force (Join-Path $root 'build')
}
New-Item -ItemType Directory -Force -Path $objDir | Out-Null
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$sources = @(
    'src\qr\qrcode.cpp',
    'src\app\core.cpp',
    'src\app\ui.cpp',
    'src\app\qrimage.cpp',
    'src\app\popup.cpp',
    'src\app\settings_win.cpp',
    'src\app\main.cpp',
    'src\app\text_util.cpp',
    'src\app\crt_stub.cpp',
    'src\app\crt_mem.c'
)

# --- resources (icon, manifest, version info) ---
$resPath = Join-Path $objDir 'app.res'
& $RcExe /nologo /fo $resPath /I (Join-Path $root 'src\res') @IncFlags (Join-Path $root 'src\res\app.rc')
if ($LASTEXITCODE -ne 0) { throw "rc.exe failed with $LASTEXITCODE" }

# --- compile + link (-GL/-LTCG keeps the static CRT compact) ---
# No C runtime: crt_stub.cpp provides the entry point, allocator and memory primitives.
# Exceptions and RTTI are off so the C++ EH runtime is never referenced.
# 0x0601 = Windows 7: everything imported is available there.
$target = if ($Arch -eq 'x86') { '/MACHINE:X86' } else { '/MACHINE:X64' }
$commonDefines = @(
    '/nologo', '/c', '/O2', '/MT', '/NODEFAULTLIB', '/GS-', '/EHs-c-', '/GR-',
    '/D_HAS_EXCEPTIONS=0', '/utf-8', '/W3',
    '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0601', '/DWINVER=0x0601',
    "/I$(Join-Path $root 'src')", "/I$(Join-Path $root 'src\res')",
    "/Fo$objDir\"
) + $IncFlags

# Everything but the CRT shim goes through LTCG.
$ltcgSources = $sources | Where-Object { $_ -notlike '*crt_mem.c' }
& $ClExe @commonDefines @('/GL') $ltcgSources
if ($LASTEXITCODE -ne 0) { throw "cl.exe failed with $LASTEXITCODE" }

# crt_mem.c defines the compiler's library helpers, which /GL cannot carry.
& $ClExe @commonDefines (Join-Path $root 'src\app\crt_mem.c')
if ($LASTEXITCODE -ne 0) { throw "cl.exe failed on crt_mem.c with $LASTEXITCODE" }

$objects = $sources | ForEach-Object {
    $leaf = (Split-Path -Leaf $_).Replace('.cpp', '.obj').Replace('.c', '.obj')
    Join-Path $objDir $leaf
}

$linkArgs = @(
    '/nologo', '/SUBSYSTEM:WINDOWS,6.01', $target, '/LTCG', '/OPT:REF', '/OPT:ICF',
    '/MANIFEST:NO', '/NODEFAULTLIB', "/ENTRY:$EntrySymbol",
    "/OUT:$exePath"
) + $LibFlags + $objects + @($resPath) + @(
    'kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib',
    'ole32.lib', 'comctl32.lib', 'uuid.lib', 'advapi32.lib', 'uxtheme.lib',
    'windowscodecs.lib'
)

& $LinkExe @linkArgs
if ($LASTEXITCODE -ne 0) { throw "link.exe failed with $LASTEXITCODE" }

$file = Get-Item $exePath
'{0}  {1:N0} bytes ({2:N1} KB)' -f $file.Name, $file.Length, ($file.Length / 1KB) | Write-Host
