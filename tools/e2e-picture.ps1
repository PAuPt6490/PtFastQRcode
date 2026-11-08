# End-to-end check for the picture link (ASCII only so any console code page can run it).
# Copies a test picture to the clipboard, fires the app hotkey, reads the link back from
# the popup field, then fetches the HTML page and the PNG over HTTP.
param(
    [string]$OutDir = 'C:\PtFastQRcode\build\e2e',
    [int]$Mods = 3,
    [int]$Vk = 81
)

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class W {
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr child, string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern int GetWindowTextW(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern int GetWindowTextLengthW(IntPtr hwnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll", ExactSpelling=true)] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
'@

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 1. A 1600x1000 test picture: larger than the 720 pixel cap, so downscaling is exercised.
$bitmap = New-Object System.Drawing.Bitmap 1600, 1000
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::FromArgb(255, 245, 245, 245))
$brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 30, 90, 200))
$graphics.FillEllipse($brush, 200, 150, 1200, 700)
$font = New-Object System.Drawing.Font('Arial', 140, [System.Drawing.FontStyle]::Bold)
$graphics.DrawString('TEST', $font, [System.Drawing.Brushes]::White, 500, 360)
$graphics.Dispose()
$sourcePath = Join-Path $OutDir 'clip-source.png'
$bitmap.Save($sourcePath, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
[System.Windows.Forms.Clipboard]::SetImage([System.Drawing.Image]::FromFile($sourcePath))
Start-Sleep -Milliseconds 200

# 2. Wake the popup through the global hotkey message the controller listens for.
$controller = [W]::FindWindowW('PtFastQRcodeController', [NullString]::Value)
if ($controller -eq [IntPtr]::Zero) { throw 'controller window not found' }

# wParam is the hotkey id (1), lParam carries mods<<16 | vk.
$lparam = ([uint32]$Mods -shl 16) -bor [uint32]$Vk
[void][W]::PostMessageW($controller, 0x0312, [IntPtr]1, [IntPtr]$lparam)

$popup = [IntPtr]::Zero
for ($i = 0; $i -lt 60 -and $popup -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 100
    $popup = [W]::FindWindowW('PtFastQRcodePopup', [NullString]::Value)
}
if ($popup -eq [IntPtr]::Zero) { throw 'popup window not found' }

$edit = [W]::FindWindowExW($popup, [IntPtr]::Zero, 'EDIT', [NullString]::Value)
if ($edit -eq [IntPtr]::Zero) { throw 'edit control not found' }
$length = [W]::GetWindowTextLengthW($edit)
$buffer = New-Object System.Text.StringBuilder ($length + 2)
[void][W]::GetWindowTextW($edit, $buffer, $length + 2)
$link = $buffer.ToString()
"field text: $link"

if ($link -notmatch '^http://\d+\.\d+\.\d+\.\d+:\d+/s/[0-9a-f]{16}$') {
    throw "unexpected field text: '$link'"
}

# 3. Fetch the HTML page and the PNG behind the same token.
$page = curl.exe -s -w " HTTPCODE:%{http_code}" $link
"page: " + ($page -join ' ')
$page -join "`n" | Set-Content -Path (Join-Path $OutDir 'page.html') -Encoding UTF8

$pngPath = Join-Path $OutDir 'served.png'
$code = curl.exe -s -o $pngPath -w "%{http_code} %{content_type} %{size_download}" "$link.png"
"png: $code"

# 4. Inspect what came back.
$image = [System.Drawing.Image]::FromFile($pngPath)
"served size: $($image.Width)x$($image.Height) format=$($image.PixelFormat)"
$image.Dispose()
"source bytes: $((Get-Item $sourcePath).Length), served bytes: $((Get-Item $pngPath).Length)"

# 5. Screenshot the popup for the record.
$rect = New-Object W+RECT
[void][W]::GetWindowRect($popup, [ref]$rect)
& powershell -NoProfile -ExecutionPolicy Bypass -File 'C:\PtFastQRcode\tools\shot-region.ps1' -X $rect.Left -Y $rect.Top -Width ($rect.Right - $rect.Left) -Height ($rect.Bottom - $rect.Top) -Out (Join-Path $OutDir 'popup-picture.png')
