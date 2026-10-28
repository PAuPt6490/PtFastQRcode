# Foreground/background memory round trip: hide via Escape, then check the fallback.
Add-Type -Namespace Cycle -Name Api -MemberDefinition @'
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int max);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
'@

function Find-Popup {
    $script:found = [IntPtr]::Zero
    $cb = [Cycle.Api+EnumProc]{
        param($h, $l)
        $sb = New-Object System.Text.StringBuilder 256
        [Cycle.Api]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq 'PtFastQRcodePopup' -and [Cycle.Api]::IsWindowVisible($h)) {
            $script:found = $h
            return $false
        }
        return $true
    }
    [Cycle.Api]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    $script:found
}

function Hotkey {
    [Cycle.Api]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
    [Cycle.Api]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 50
    [Cycle.Api]::keybd_event(0x51, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 50
    [Cycle.Api]::keybd_event(0x51, 0, 2, [UIntPtr]::Zero)
    [Cycle.Api]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    [Cycle.Api]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
}

function Report([string]$tag) {
    $p = Get-Process PtFastQRcode
    $p.Refresh()
    '{0,-14} 私有 {1,7:N0} KB   工作集 {2,5:N1} MB   句柄 {3}   线程 {4}' -f $tag,
        ($p.PrivateMemorySize64 / 1KB), ($p.WorkingSet64 / 1MB), $p.HandleCount, $p.Threads.Count
}

Report '托盘'
for ($i = 1; $i -le 3; $i++) {
    Hotkey
    Start-Sleep -Milliseconds 900
    Report "开窗 $i"
    [Cycle.Api]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
    [Cycle.Api]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 900
    Report "回后台 $i"
}
