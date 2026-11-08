# Type ASCII text into the guest desktop through injected scancodes.
param(
    [Parameter(Mandatory=$true)][string]$Text,
    [string]$Vm = 'Win7x86'
)
$vbox = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
$codes = (python "$PSScriptRoot\scancodes.py" $Text).Trim().Split(' ')
$chunk = 12
for ($i = 0; $i -lt $codes.Length; $i += $chunk) {
    $slice = $codes[$i..([Math]::Min($i + $chunk - 1, $codes.Length - 1))]
    & $vbox controlvm $Vm keyboardputscancode $slice | Out-Null
    Start-Sleep -Milliseconds 25
}
