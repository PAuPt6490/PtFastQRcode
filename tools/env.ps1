# Shared toolchain paths for the native build (MSVC + Windows SDK are already installed).
$global:VcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231'
$global:SdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$global:SdkVer = '10.0.26100.0'
$global:ClExe = Join-Path $VcRoot 'bin\Hostx64\x64\cl.exe'
$global:LinkExe = Join-Path $VcRoot 'bin\Hostx64\x64\link.exe'
$global:RcExe = Join-Path $SdkRoot "bin\$SdkVer\x64\rc.exe"
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
    "/LIBPATH:`"$VcRoot\lib\x64`"",
    "/LIBPATH:`"$SdkLib\um\x64`"",
    "/LIBPATH:`"$SdkLib\ucrt\x64`""
)
