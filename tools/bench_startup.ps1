# Cold-start latency: process start -> popup window visible.
param([int]$Runs = 5)

Add-Type -Namespace Startup -Name Api -MemberDefinition @'
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int max);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
'@
[Startup.Api]::SetProcessDPIAware() | Out-Null

function Find-Visible([string]$class) {
    $script:found = [IntPtr]::Zero
    $cb = [Startup.Api+EnumProc]{
        param($h, $l)
        $sb = New-Object System.Text.StringBuilder 256
        [Startup.Api]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq $class -and [Startup.Api]::IsWindowVisible($h)) {
            $script:found = $h
            return $false
        }
        return $true
    }
    [Startup.Api]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    $script:found
}

$exe = 'C:\PtFastQRcode\dist\PtFastQRcode.exe'
$times = @()
for ($i = 0; $i -lt $Runs; $i++) {
    Get-Process PtFastQRcode -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 400
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Start-Process -FilePath $exe -ArgumentList '--show' | Out-Null
    $elapsed = -1
    while ($watch.ElapsedMilliseconds -lt 3000) {
        if ((Find-Visible 'PtFastQRcodePopup') -ne [IntPtr]::Zero) {
            $elapsed = $watch.ElapsedMilliseconds
            break
        }
        Start-Sleep -Milliseconds 5
    }
    $times += $elapsed
    "run {0}: {1} ms" -f ($i + 1), $elapsed
}
$valid = $times | Where-Object { $_ -ge 0 }
"平均 {0:N1} ms, 最小 {1} ms, 最大 {2} ms" -f ($valid | Measure-Object -Average).Average, ($valid | Measure-Object -Minimum).Minimum, ($valid | Measure-Object -Maximum).Maximum
