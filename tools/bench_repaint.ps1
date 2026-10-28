# Measures full-window repaint cost and startup latency of the running build.
param(
    [string]$Class = 'PtFastQRcodePopup',
    [int]$Repaints = 300
)

Add-Type -Namespace Perf -Name Api -MemberDefinition @'
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int max);
[DllImport("user32.dll")] public static extern bool InvalidateRect(IntPtr h, IntPtr r, bool erase);
[DllImport("user32.dll")] public static extern bool UpdateWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
'@
[Perf.Api]::SetProcessDPIAware() | Out-Null

function Find-Window([string]$class) {
    $script:found = [IntPtr]::Zero
    $cb = [Perf.Api+EnumProc]{
        param($h, $l)
        $sb = New-Object System.Text.StringBuilder 256
        [Perf.Api]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq $class) { $script:found = $h; return $false }
        return $true
    }
    [Perf.Api]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    $script:found
}

$popup = Find-Window $Class
if ($popup -eq [IntPtr]::Zero) { Write-Error 'popup not found'; exit 1 }

# warm-up
for ($i = 0; $i -lt 30; $i++) {
    [Perf.Api]::InvalidateRect($popup, [IntPtr]::Zero, $false) | Out-Null
    [Perf.Api]::UpdateWindow($popup) | Out-Null
}

$process = Get-Process PtFastQRcode
$before = $process.TotalProcessorTime.TotalMilliseconds
for ($i = 0; $i -lt $Repaints; $i++) {
    [Perf.Api]::InvalidateRect($popup, [IntPtr]::Zero, $false) | Out-Null
    [Perf.Api]::UpdateWindow($popup) | Out-Null
}
$process.Refresh()
$after = $process.TotalProcessorTime.TotalMilliseconds

$client = New-Object Perf.Api+RECT
[Perf.Api]::GetClientRect($popup, [ref]$client) | Out-Null

'{0} x {1} full repaints: {2:N1} ms total, {3:N3} ms per frame' -f $client.Right, $client.Bottom, ($after - $before), (($after - $before) / $Repaints)
