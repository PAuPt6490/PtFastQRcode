# Captures a PtFastQRcode window at physical resolution (DPI aware).
# Uses PrintWindow so the shot is correct even when the window is partially covered.
#   powershell -File tools\capture.ps1 -Class PtFastQRcodePopup -Out tools\shot.png
param(
    [string]$Class = 'PtFastQRcodePopup',
    [string]$Out = 'tools\shot.png'
)

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace FastShot -Name Api -MemberDefinition @'
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int max);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int index);
public struct RECT { public int Left, Top, Right, Bottom; }
'@

[FastShot.Api]::SetProcessDPIAware() | Out-Null
$found = [IntPtr]::Zero
$cb = [FastShot.Api+EnumProc]{
    param($h, $l)
    $sb = New-Object System.Text.StringBuilder 256
    [FastShot.Api]::GetClassName($h, $sb, 256) | Out-Null
    if ($sb.ToString() -eq $Class) {
        $script:found = $h
        return $false
    }
    return $true
}
[FastShot.Api]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null

if ($found -eq [IntPtr]::Zero) { Write-Error "window $Class not found"; exit 1 }

$rect = New-Object FastShot.Api+RECT
[FastShot.Api]::GetWindowRect($found, [ref]$rect) | Out-Null
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top

$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$hdc = $graphics.GetHdc()
$ok = [FastShot.Api]::PrintWindow($found, $hdc, 2)   # PW_RENDERFULLCONTENT
$graphics.ReleaseHdc($hdc)
$graphics.Dispose()

if (-not $ok) {
    [FastShot.Api]::SetForegroundWindow($found) | Out-Null
    [FastShot.Api]::BringWindowToTop($found) | Out-Null
    Start-Sleep -Milliseconds 250
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, (New-Object System.Drawing.Size $width, $height))
    $graphics.Dispose()
    Write-Host "used screen capture"
}

$path = Join-Path (Get-Location) $Out
$bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Host "saved $path ($width x $height)"
