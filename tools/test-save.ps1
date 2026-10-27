# End-to-end save test: hotkey -> auto paste -> Ctrl+S -> Enter -> verify PNG.
#   powershell -File tools\test-save.ps1 -Text "https://example.com/test" -Expect out.png
param(
    [string]$Text = 'https://example.com/save-test?ts=20261027'
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -Namespace SaveTest -Name Api -MemberDefinition @'
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int max);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr lParam);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
'@

function Find-Class([string]$class) {
    $script:found = [IntPtr]::Zero
    $cb = [SaveTest.Api+EnumProc]{
        param($h, $l)
        $sb = New-Object System.Text.StringBuilder 256
        [SaveTest.Api]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq $class) { $script:found = $h; return $false }
        return $true
    }
    [SaveTest.Api]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    return $script:found
}

function Key([int]$vk, [int]$up) { [SaveTest.Api]::keybd_event([byte]$vk, 0, [uint32]$up, [UIntPtr]::Zero) }
function Hotkey { Key 0x11 0; Key 0x12 0; Start-Sleep -Milliseconds 50; Key 0x51 0; Start-Sleep -Milliseconds 50; Key 0x51 2; Key 0x12 2; Key 0x11 2 }

$app = Get-Process PtFastQRcode -ErrorAction SilentlyContinue
if (-not $app) { Write-Error 'PtFastQRcode is not running'; exit 1 }

# Make sure the popup starts hidden so the hotkey opens (and does not toggle) it.
$popup = Find-Class 'PtFastQRcodePopup'
if ($popup -ne [IntPtr]::Zero -and [SaveTest.Api]::IsWindowVisible($popup)) {
    [SaveTest.Api]::PostMessage($popup, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 400
}

Set-Clipboard -Value $Text
Hotkey
Start-Sleep -Milliseconds 900

$popup = Find-Class 'PtFastQRcodePopup'
$visible = ($popup -ne [IntPtr]::Zero) -and [SaveTest.Api]::IsWindowVisible($popup)
"popup visible: $visible"
if (-not $visible) { exit 2 }

Key 0x11 0; Start-Sleep -Milliseconds 60; Key 0x53 0; Start-Sleep -Milliseconds 60; Key 0x53 2; Key 0x11 2
Start-Sleep -Milliseconds 1200

$dialog = Find-Class '#32770'
"save dialog: $dialog"
if ($dialog -eq [IntPtr]::Zero) { exit 3 }

Key 0x0D 0; Start-Sleep -Milliseconds 80; Key 0x0D 2
Start-Sleep -Milliseconds 1600

$dir = Join-Path $env:USERPROFILE 'Pictures\PtFastQRcode'
if (-not (Test-Path $dir)) { "save folder missing: $dir"; exit 4 }
$file = Get-ChildItem $dir -File -Filter *.png | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $file) { "no png saved"; exit 5 }
"$($file.FullName)"
"$($file.Length) bytes"
