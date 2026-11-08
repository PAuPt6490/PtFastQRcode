# ASCII only. Shoots docs/popup-picture-link.png with a picture on the clipboard.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$previous = Get-ItemProperty 'HKCU:\Software\PtFastQRcode'
Get-Process PtFastQRcode -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

Set-ItemProperty 'HKCU:\Software\PtFastQRcode' -Name ImageServer -Value 1 -Type DWord
Set-ItemProperty 'HKCU:\Software\PtFastQRcode' -Name Theme -Value 1 -Type DWord

powershell -STA -NoProfile -ExecutionPolicy Bypass -File tools\set-clip-image.ps1 -Style test | Out-Null
Start-Sleep -Milliseconds 600
Start-Process -FilePath (Join-Path $root 'dist\PtFastQRcode.exe') -ArgumentList '--show'
Start-Sleep -Seconds 3
powershell -NoProfile -ExecutionPolicy Bypass -File tools\capture.ps1 -Class PtFastQRcodePopup -Out docs\popup-picture-link.png

Get-Process PtFastQRcode -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
Set-ItemProperty 'HKCU:\Software\PtFastQRcode' -Name ImageServer -Value $previous.ImageServer -Type DWord
Set-ItemProperty 'HKCU:\Software\PtFastQRcode' -Name Theme -Value $previous.Theme -Type DWord
Start-Process -FilePath (Join-Path $root 'dist\PtFastQRcode.exe') -ArgumentList '--show'
Write-Output 'picture-link shot done'
