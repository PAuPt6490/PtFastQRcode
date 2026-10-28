# Measures private memory / working set before and after the common save dialog.
param([int]$SettleSeconds = 8)

Add-Type -Namespace MemProbe -Name Api -MemberDefinition @'
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int max);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
'@

function Find-Class([string]$class) {
    $script:found = [IntPtr]::Zero
    $cb = [MemProbe.Api+EnumProc]{
        param($h, $l)
        $sb = New-Object System.Text.StringBuilder 256
        [MemProbe.Api]::GetClassName($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq $class) { $script:found = $h; return $false }
        return $true
    }
    [MemProbe.Api]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    $script:found
}

function Report([string]$tag) {
    $p = Get-Process PtFastQRcode
    $p.Refresh()
    '{0,-16} 私有 {1,7:N0} KB   工作集 {2,5:N1} MB   虚拟 {3,6:N0} MB' -f $tag,
        ($p.PrivateMemorySize64 / 1KB), ($p.WorkingSet64 / 1MB), ($p.VirtualMemorySize64 / 1MB)
}

Report '起始'
$popup = Find-Class 'PtFastQRcodePopup'
if ($popup -eq [IntPtr]::Zero) { Write-Error 'popup missing'; exit 1 }
[MemProbe.Api]::PostMessage($popup, 0x0111, [IntPtr]2002, [IntPtr]::Zero) | Out-Null   # 保存图片
Start-Sleep -Milliseconds 1500
Report '对话框打开'
# close the dialog with Esc
Add-Type -Namespace MemProbe2 -Name Api -MemberDefinition '[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);'
[MemProbe2.Api]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
[MemProbe2.Api]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 1200
Report '关闭后 1s'
Start-Sleep -Seconds $SettleSeconds
Report "静置 ${SettleSeconds}s"
