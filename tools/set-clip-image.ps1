# Put a test picture on the host clipboard so a VirtualBox guest can paste it.
# Must run in an STA apartment: powershell -STA -NoProfile -File tools\set-clip-image.ps1
# -Style split paints a red/green half split, which makes it easy to check a thumbnail
# visually or by sampling pixels.
param([int]$Width = 1200, [int]$Height = 800, [string]$Out = '', [string]$Style = 'test')

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$bmp = New-Object System.Drawing.Bitmap $Width, $Height
$g = [System.Drawing.Graphics]::FromImage($bmp)

if ($Style -eq 'split') {
    $g.FillRectangle([System.Drawing.Brushes]::Red, 0, 0, [int]($Width / 2), $Height)
    $g.FillRectangle([System.Drawing.Brushes]::Lime, [int]($Width / 2), 0, [int]($Width / 2), $Height)
} else {
    $g.Clear([System.Drawing.Color]::FromArgb(24, 118, 196))
    $g.FillEllipse([System.Drawing.Brushes]::Orange, [int]($Width * 0.15), [int]($Height * 0.15), [int]($Width * 0.5), [int]($Height * 0.55))
    $font = New-Object System.Drawing.Font 'Segoe UI', 64
    $g.DrawString('PICTURE TEST', $font, [System.Drawing.Brushes]::White, [int]($Width * 0.1), [int]($Height * 0.72))
}
$g.Dispose()

if ($Out) { $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png) }

[System.Windows.Forms.Clipboard]::SetImage($bmp)
$bmp.Dispose()
'clipboard image: ' + [System.Windows.Forms.Clipboard]::ContainsImage()
