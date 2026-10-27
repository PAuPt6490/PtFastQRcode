# Captures a screen region (physical pixels, DPI aware) for menu/overlay checks.
#   powershell -File tools\shot-region.ps1 -X 500 -Y 400 -Width 420 -Height 520 -Out tools\menu.png
param(
    [int]$X = 0,
    [int]$Y = 0,
    [int]$Width = 400,
    [int]$Height = 400,
    [string]$Out = 'tools\region.png'
)

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace RegionShot -Name Api -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
'@
[RegionShot.Api]::SetProcessDPIAware() | Out-Null

$bitmap = New-Object System.Drawing.Bitmap $Width, $Height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($X, $Y, 0, 0, (New-Object System.Drawing.Size $Width, $Height))
$graphics.Dispose()
$path = Join-Path (Get-Location) $Out
$bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Host "saved $path"
