# TTL check: publish a picture link, confirm it serves, then confirm it 404s after five minutes.
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class T {
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern IntPtr FindWindowW(string c, string t);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static string Text(IntPtr h) {
        var sb = new StringBuilder(512);
        SendMessageW(h, 0x000D, (IntPtr)512, sb);
        return sb.ToString();
    }
}
'@ | Out-Null

$bitmap = New-Object System.Drawing.Bitmap 1600, 1000
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::FromArgb(255, 245, 245, 245))
$graphics.FillEllipse([System.Drawing.Brushes]::SteelBlue, 200, 150, 1200, 700)
$graphics.Dispose()
[System.Windows.Forms.Clipboard]::SetImage($bitmap)

$controller = [T]::FindWindowW('PtFastQRcodeController', [NullString]::Value)
if ($controller -eq [IntPtr]::Zero) { "FAIL: controller not running"; exit 1 }

# Close any popup left open so the hotkey opens a fresh one.
$stale = [T]::FindWindowW('PtFastQRcodePopup', [NullString]::Value)
if ($stale -ne [IntPtr]::Zero) { [void][T]::PostMessageW($stale, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero); Start-Sleep -Milliseconds 400 }

[void][T]::PostMessageW($controller, 0x0312, [IntPtr]1, [IntPtr]0)
$popup = [IntPtr]::Zero
for ($i = 0; $i -lt 100 -and $popup -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 50
    $popup = [T]::FindWindowW('PtFastQRcodePopup', [NullString]::Value)
}
if ($popup -eq [IntPtr]::Zero) { "FAIL: popup did not open"; exit 1 }
Start-Sleep -Milliseconds 600
$edit = [T]::FindWindowExW($popup, [IntPtr]::Zero, 'EDIT', [NullString]::Value)
$url = [T]::Text($edit)
"link: $url"
if ($url -notmatch '^http://\d+\.\d+\.\d+\.\d+:\d+/s/[0-9a-f]{16}$') { "FAIL: unexpected field text"; exit 1 }

$pageNow = curl.exe -s -o NUL -w "%{http_code}" $url
$pngNow = curl.exe -s -o NUL -w "%{http_code}" "$url.png"
"fresh: page=$pageNow png=$pngNow"
if ($pageNow -ne '200' -or $pngNow -ne '200') { "FAIL: fresh link did not serve"; exit 1 }

Start-Sleep -Seconds 320
$pageLater = curl.exe -s -o NUL -w "%{http_code}" $url
$pngLater = curl.exe -s -o NUL -w "%{http_code}" "$url.png"
"after 5min: page=$pageLater png=$pngLater"
if ($pageLater -ne '404' -or $pngLater -ne '404') { "FAIL: expired link still served"; exit 1 }
"PASS"
