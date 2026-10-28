# Opens and closes the settings window repeatedly, reporting latency and handle growth.
param([int]$Cycles = 5)

Add-Type -Namespace SetBench -Name Api -MemberDefinition @'
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int max);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr h, uint f);
[DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int a, bool b, int id);
'@

function Find-Class([string]$class) {
    $script:found = [IntPtr]::Zero
    $cb = [SetBench.Api+EnumProc]{
        param($h, $l)
        $sb = New-Object System.Text.StringBuilder 256
        [SetBench.Api]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq $class) { $script:found = $h; return $false }
        return $true
    }
    [SetBench.Api]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    $script:found
}

function Stats {
    $p = Get-Process PtFastQRcode
    $h = [SetBench.Api]::OpenProcess(0x1000, $false, $p.Id)
    '{0,-10} 私有 {1,7:N0} KB   WS {2,5:N1} MB   GDI {3}  USER {4}' -f 'state', ($p.PrivateMemorySize64/1KB), ($p.WorkingSet64/1MB), [SetBench.Api]::GetGuiResources($h,0), [SetBench.Api]::GetGuiResources($h,1)
}

$popup = Find-Class 'PtFastQRcodePopup'
if ($popup -eq [IntPtr]::Zero) { Write-Error 'popup missing'; exit 1 }

$latencies = @()
for ($i = 0; $i -lt $Cycles; $i++) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [SetBench.Api]::PostMessage($popup, 0x0111, [IntPtr]2005, [IntPtr]::Zero) | Out-Null
    $opened = -1
    while ($watch.ElapsedMilliseconds -lt 2000) {
        if ((Find-Class 'PtFastQRcodeSettings') -ne [IntPtr]::Zero) { $opened = $watch.ElapsedMilliseconds; break }
        Start-Sleep -Milliseconds 2
    }
    $latencies += $opened
    Start-Sleep -Milliseconds 250
    $settings = Find-Class 'PtFastQRcodeSettings'
    [SetBench.Api]::PostMessage($settings, 0x0111, [IntPtr]3062, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 250
    '{0}: 打开 {1} ms' -f $i, $opened
    Stats
}
$valid = $latencies | Where-Object { $_ -ge 0 }
'打开延迟 平均 {0:N1} ms / 最小 {1} / 最大 {2}' -f ($valid | Measure-Object -Average).Average, ($valid | Measure-Object -Minimum).Minimum, ($valid | Measure-Object -Maximum).Maximum
