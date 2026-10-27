# Tray icon interaction test: locates the icon with Shell_NotifyIconGetRect (official API),
# sends a real mouse click, and reports whether the popup or the context menu appeared.
#   powershell -File tools\test-tray.ps1 -Button left|right
param(
    [string]$Button = 'right'
)

Add-Type -Namespace TrayTest -Name Api -MemberDefinition @'
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[StructLayout(LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
[StructLayout(LayoutKind.Sequential)]
public struct NOTIFYICONIDENTIFIER { public int cbSize; public IntPtr hWnd; public uint uID; public Guid guidItem; }
[DllImport("shell32.dll", CharSet=CharSet.Unicode)]
public static extern int Shell_NotifyIconGetRect(ref NOTIFYICONIDENTIFIER identifier, out RECT iconLocation);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int max);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
'@

[TrayTest.Api]::SetProcessDPIAware() | Out-Null

function Find-Class([string]$class) {
    $script:found = [IntPtr]::Zero
    $cb = [TrayTest.Api+EnumProc]{
        param($h, $l)
        $sb = New-Object System.Text.StringBuilder 256
        [TrayTest.Api]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq $class) { $script:found = $h; return $false }
        return $true
    }
    [TrayTest.Api]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    return $script:found
}

$controller = Find-Class 'PtFastQRcodeController'
if ($controller -eq [IntPtr]::Zero) { Write-Error 'PtFastQRcode is not running'; exit 1 }

$id = New-Object TrayTest.Api+NOTIFYICONIDENTIFIER
$id.cbSize = [Runtime.InteropServices.Marshal]::SizeOf([type][TrayTest.Api+NOTIFYICONIDENTIFIER])
$id.hWnd = $controller
$id.uID = 1
$rect = New-Object TrayTest.Api+RECT
$hr = [TrayTest.Api]::Shell_NotifyIconGetRect([ref]$id, [ref]$rect)
if ($hr -ne 0) { "Shell_NotifyIconGetRect failed hr=0x{0:X8} (icon may be inside the overflow flyout)" -f $hr; exit 2 }

$x = [int](($rect.Left + $rect.Right) / 2)
$y = [int](($rect.Top + $rect.Bottom) / 2)
"tray icon rect: $($rect.Left),$($rect.Top) $($rect.Right-$rect.Left)x$($rect.Bottom-$rect.Top) -> click $x,$y"

# hide the popup first so a leftover visible window cannot confuse the result
$popup = Find-Class 'PtFastQRcodePopup'
if ($popup -ne [IntPtr]::Zero -and [TrayTest.Api]::IsWindowVisible($popup)) {
    [TrayTest.Api]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    [TrayTest.Api]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
}

[TrayTest.Api]::SetCursorPos($x, $y) | Out-Null
Start-Sleep -Milliseconds 150

if ($Button -eq 'right') {
    [TrayTest.Api]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero)   # right down
    Start-Sleep -Milliseconds 80
    [TrayTest.Api]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero)   # right up
    Start-Sleep -Milliseconds 700
    $menu = Find-Class '#32768'
    if ($menu -ne [IntPtr]::Zero -and [TrayTest.Api]::IsWindowVisible($menu)) {
        $mr = New-Object TrayTest.Api+RECT
        [TrayTest.Api]::GetWindowRect($menu, [ref]$mr) | Out-Null
        "context menu visible at $($mr.Left),$($mr.Top) size $($mr.Right-$mr.Left)x$($mr.Bottom-$mr.Top)"
        # dismiss
        Add-Type -Namespace TrayTestKey -Name A -MemberDefinition '[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);'
        [TrayTestKey.A]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
        [TrayTestKey.A]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
        exit 0
    }
    'context menu NOT shown'
    exit 3
} else {
    [TrayTest.Api]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)   # left down
    Start-Sleep -Milliseconds 80
    [TrayTest.Api]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)   # left up
    Start-Sleep -Milliseconds 800
    $popup = Find-Class 'PtFastQRcodePopup'
    $visible = ($popup -ne [IntPtr]::Zero) -and [TrayTest.Api]::IsWindowVisible($popup)
    "popup visible after left click: $visible"
    if ($visible) { exit 0 } else { exit 4 }
}
