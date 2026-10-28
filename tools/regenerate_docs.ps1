# Regenerates docs screenshots with the renamed app (PtFastQRcode).
# Windows are captured with PrintWindow, so overlapping windows do not pollute the shots.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot   # 仓库根目录,脚本放在 tools\ 下
Set-Location $root

Add-Type -Namespace Shot -Name Api -MemberDefinition @'
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int max);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
'@
[Shot.Api]::SetProcessDPIAware() | Out-Null
Add-Type -AssemblyName System.Drawing

function Find-Class([string]$class) {
    $script:found = [IntPtr]::Zero
    $cb = [Shot.Api+EnumProc]{
        param($h, $l)
        $sb = New-Object System.Text.StringBuilder 256
        [Shot.Api]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq $class) { $script:found = $h; return $false }
        return $true
    }
    [Shot.Api]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    $script:found
}

function Capture-Rect([int]$x, [int]$y, [int]$w, [int]$h, [string]$out) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $g.Dispose()
    $bmp.Save((Join-Path $root $out), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

function Capture-Window([string]$class, [string]$out) {
    $hwnd = Find-Class $class
    if ($hwnd -eq [IntPtr]::Zero) { throw "window $class not found" }
    $r = New-Object Shot.Api+RECT
    [Shot.Api]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left
    $h = $r.Bottom - $r.Top
    [Shot.Api]::SetForegroundWindow($hwnd) | Out-Null
    [Shot.Api]::BringWindowToTop($hwnd) | Out-Null
    Start-Sleep -Milliseconds 350
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [Shot.Api]::PrintWindow($hwnd, $hdc, 2)   # PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    if (-not $ok) {
        $g2 = [System.Drawing.Graphics]::FromImage($bmp)
        $g2.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $h))
        $g2.Dispose()
    }
    $bmp.Save((Join-Path $root $out), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    "saved $out"
}

function Stop-App {
    Get-Process PtFastQRcode -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 400
}

function Set-Theme([int]$theme) {
    # 0 = Win32 classic, 1 = follow system, 2 = WinUI light, 3 = WinUI dark
    Set-ItemProperty 'HKCU:\Software\PtFastQRcode' -Name Theme -Value $theme -Type DWord
}

function Start-App([string]$arguments) {
    Start-Process -FilePath (Join-Path $root 'dist\PtFastQRcode.exe') -ArgumentList $arguments
    Start-Sleep -Seconds 2
}

$previous = Get-ItemProperty 'HKCU:\Software\PtFastQRcode'
Set-Clipboard -Value 'https://github.com/PAuPt6490/PtFastQRcode'

# 1. 默认外观(WinUI 3 + 跟随系统)
Stop-App; Set-Theme 1; Start-App '--show'
Capture-Window 'PtFastQRcodePopup' 'docs\popup-default.png'

# 2. 深色(WinUI 3 + 深色)
Stop-App; Set-Theme 3; Start-App '--show'
Capture-Window 'PtFastQRcodePopup' 'docs\popup-dark.png'

# 3. Win32 经典(浅色)
Stop-App; Set-Theme 0; Start-App '--show'
Capture-Window 'PtFastQRcodePopup' 'docs\popup-win32.png'

# 4. 设置窗口(深色,展示两种风格)
Stop-App; Set-Theme 3; Start-App '--show'
Start-Sleep -Milliseconds 600
$popup = Find-Class 'PtFastQRcodePopup'
[Shot.Api]::PostMessage($popup, 0x0111, [IntPtr]2005, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 1000
Capture-Window 'PtFastQRcodeSettings' 'docs\settings.png'

# 5. 托盘菜单(菜单必须抓屏,它本身总在最前)
Stop-App; Set-Theme 0; Start-App '--startup'
$controller = Find-Class 'PtFastQRcodeController'
[Shot.Api]::SetCursorPos(900, 700) | Out-Null
Start-Sleep -Milliseconds 200
$packed = [IntPtr]([int64](0x00010000 -bor 0x007B))   # NOTIFYICON_VERSION_4: LOWORD=事件, HIWORD=图标 id
[Shot.Api]::PostMessage($controller, 0x8000 + 11, [IntPtr]::Zero, $packed) | Out-Null
Start-Sleep -Milliseconds 800
$menu = Find-Class '#32768'
$mr = New-Object Shot.Api+RECT
[Shot.Api]::GetWindowRect($menu, [ref]$mr) | Out-Null
Capture-Rect $mr.Left $mr.Top ($mr.Right - $mr.Left) ($mr.Bottom - $mr.Top) 'docs\tray-menu.png'
"tray menu: $($mr.Right - $mr.Left)x$($mr.Bottom - $mr.Top)"
Add-Type -Namespace ShotKey -Name A -MemberDefinition '[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);'
[ShotKey.A]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
[ShotKey.A]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)

# 恢复用户原来的外观设置并启动
Stop-App
Set-Theme ([int]$previous.Theme)
Start-App '--show'
"restored theme to $($previous.Theme)"
