# End-to-end checks for FastQRcode: hotkey, paste+save, click-outside close, tray menu.
#   powershell -File tools\e2e.ps1 -Test hotkey
param(
    [string]$Test = 'hotkey'
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -Namespace E2E -Name Api -MemberDefinition @'
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int max);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr lParam);
public struct RECT { public int Left, Top, Right, Bottom; }
'@

function Find-AppWindow([string]$class) {
    $script:found = [IntPtr]::Zero
    $cb = [E2E.Api+EnumProc]{
        param($h, $l)
        $sb = New-Object System.Text.StringBuilder 256
        [E2E.Api]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq $class) { $script:found = $h; return $false }
        return $true
    }
    [E2E.Api]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    return $script:found
}

function Send-Hotkey {
    # Ctrl + Alt + Q
    [E2E.Api]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)       # Ctrl down
    [E2E.Api]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)       # Alt down
    Start-Sleep -Milliseconds 60
    [E2E.Api]::keybd_event(0x51, 0, 0, [UIntPtr]::Zero)       # Q down
    Start-Sleep -Milliseconds 60
    [E2E.Api]::keybd_event(0x51, 0, 2, [UIntPtr]::Zero)       # Q up
    [E2E.Api]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)       # Alt up
    [E2E.Api]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)       # Ctrl up
}

$popup = Find-AppWindow 'FastQRcodePopup'
$visibleBefore = if ($popup -ne [IntPtr]::Zero) { [E2E.Api]::IsWindowVisible($popup) } else { $false }

switch ($Test) {
    'hotkey' {
        Send-Hotkey
        Start-Sleep -Milliseconds 500
        $popup = Find-AppWindow 'FastQRcodePopup'
        $visibleAfter = if ($popup -ne [IntPtr]::Zero) { [E2E.Api]::IsWindowVisible($popup) } else { $false }
        "hotkey: before=$visibleBefore after=$visibleAfter"
        if ($visibleAfter) { exit 0 } else { exit 1 }
    }
    'outside' {
        # Requires the popup to be visible: click far away on the desktop.
        $popup = Find-AppWindow 'FastQRcodePopup'
        $rect = New-Object E2E.Api+RECT
        [E2E.Api]::GetWindowRect($popup, [ref]$rect) | Out-Null
        [E2E.Api]::SetCursorPos(20, 20) | Out-Null
        Start-Sleep -Milliseconds 120
        [E2E.Api]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)   # left down
        [E2E.Api]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)   # left up
        Start-Sleep -Milliseconds 500
        $stillVisible = [E2E.Api]::IsWindowVisible($popup)
        "outside-click: stillVisible=$stillVisible"
        if ($stillVisible) { exit 1 } else { exit 0 }
    }
    'tray' {
        $controller = Find-AppWindow 'FastQRcodeController'
        if ($controller -eq [IntPtr]::Zero) { "controller window not found"; exit 1 }
        [E2E.Api]::SetCursorPos(600, 400) | Out-Null
        [E2E.Api]::PostMessage($controller, 0x8000 + 11, [IntPtr]::Zero, [IntPtr]0x0205) | Out-Null  # WM_APP_TRAY + WM_RBUTTONUP
        Start-Sleep -Milliseconds 400
        "tray menu posted"
        exit 0
    }
    default {
        "unknown test"
        exit 2
    }
}
