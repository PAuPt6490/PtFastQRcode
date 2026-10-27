# Finds a FastQRcode window handle by class name (DPI independent) and optionally posts a command.
#   powershell -File tools\window.ps1 -Class FastQRcodePopup
#   powershell -File tools\window.ps1 -Class FastQRcodePopup -Command 2005
param(
    [string]$Class = 'FastQRcodePopup',
    [int]$Command = 0
)

Add-Type -Namespace WinFind -Name Api -MemberDefinition @'
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int max);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
'@

$script:handle = [IntPtr]::Zero
$callback = [WinFind.Api+EnumProc]{
    param($h, $l)
    $buffer = New-Object System.Text.StringBuilder 256
    [WinFind.Api]::GetClassName($h, $buffer, 256) | Out-Null
    if ($buffer.ToString() -eq $Class) {
        $script:handle = $h
        return $false
    }
    return $true
}
[WinFind.Api]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null

if ($script:handle -eq [IntPtr]::Zero) {
    Write-Output "notfound"
    exit 1
}
if ($Command -ne 0) {
    [WinFind.Api]::PostMessage($script:handle, 0x0111, [IntPtr]$Command, [IntPtr]::Zero) | Out-Null
    Write-Output "sent $Command"
} else {
    Write-Output "$($script:handle)"
}
