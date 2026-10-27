# Builds PtFastQRcode as a single statically linked Win32 executable.
# Toolchain: MSVC + Windows SDK already present on this machine (see tools\env.ps1).
#
#   pwsh -File build.ps1            # release build into .\dist
#   pwsh -File build.ps1 -Clean     # wipe intermediate objects first

param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

. (Join-Path $root 'tools\env.ps1')

$objDir = Join-Path $root 'build\obj'
$outDir = Join-Path $root 'dist'
$exePath = Join-Path $outDir 'PtFastQRcode.exe'

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
    'src\app\main.cpp'
)

# --- resources (icon, manifest, version info) ---
$resPath = Join-Path $objDir 'app.res'
& $RcExe /nologo /fo $resPath /I (Join-Path $root 'src\res') @IncFlags (Join-Path $root 'src\res\app.rc')
if ($LASTEXITCODE -ne 0) { throw "rc.exe failed with $LASTEXITCODE" }

# --- compile + link (-GL/-LTCG keeps the static CRT compact) ---
$compileArgs = @(
    '/nologo', '/c', '/O2', '/GL', '/MT', '/EHsc', '/utf-8', '/W3',
    '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00',
    "/I$(Join-Path $root 'src')", "/I$(Join-Path $root 'src\res')",
    "/Fo$objDir\"
) + $IncFlags + $sources

& $ClExe @compileArgs
if ($LASTEXITCODE -ne 0) { throw "cl.exe failed with $LASTEXITCODE" }

$objects = $sources | ForEach-Object {
    Join-Path $objDir ((Split-Path -Leaf $_).Replace('.cpp', '.obj'))
}

$linkArgs = @(
    '/nologo', '/SUBSYSTEM:WINDOWS', '/LTCG', '/OPT:REF', '/OPT:ICF', '/MANIFEST:NO',
    "/OUT:$exePath"
) + $LibFlags + $objects + @($resPath) + @(
    'user32.lib', 'gdi32.lib', 'gdiplus.lib', 'shell32.lib', 'shlwapi.lib',
    'ole32.lib', 'comctl32.lib', 'comdlg32.lib', 'uuid.lib', 'advapi32.lib'
)

& $LinkExe @linkArgs
if ($LASTEXITCODE -ne 0) { throw "link.exe failed with $LASTEXITCODE" }

$file = Get-Item $exePath
'{0}  {1:N0} bytes ({2:N1} KB)' -f $file.Name, $file.Length, ($file.Length / 1KB) | Write-Host
