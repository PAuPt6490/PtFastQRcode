# Shared toolchain paths for the native build (MSVC + Windows SDK are already installed).
# Dot-source with -Arch x64 (default) or -Arch x86.
param(
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64'
)

$global:VcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231'
$global:SdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$global:SdkVer = '10.0.26100.0'
$global:TargetArch = $Arch

# x86 is a cross build: x64 host tools emitting 32-bit objects.
$hostDir = if ($Arch -eq 'x86') { 'Hostx64\x86' } else { 'Hostx64\x64' }
$global:ClExe = Join-Path $VcRoot "bin\$hostDir\cl.exe"
$global:LinkExe = Join-Path $VcRoot "bin\$hostDir\link.exe"
$global:RcExe = Join-Path $SdkRoot "bin\$SdkVer\$Arch\rc.exe"
$global:SdkInc = Join-Path $SdkRoot "Include\$SdkVer"
$global:SdkLib = Join-Path $SdkRoot "Lib\$SdkVer"

$global:IncFlags = @(
    "/I`"$VcRoot\include`"",
    "/I`"$SdkInc\ucrt`"",
    "/I`"$SdkInc\shared`"",
    "/I`"$SdkInc\um`"",
    "/I`"$SdkInc\winrt`""
)
$global:LibFlags = @(
    "/LIBPATH:`"$VcRoot\lib\$Arch`"",
    "/LIBPATH:`"$SdkLib\um\$Arch`"",
    "/LIBPATH:`"$SdkLib\ucrt\$Arch`""
)

# The linker prepends the x86 underscore itself, so the undecorated name is correct
# for both architectures.
$global:EntrySymbol = 'entryPoint'
