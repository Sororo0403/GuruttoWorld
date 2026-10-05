# Render the tourist gate's farewell sign using the bundled Japanese font.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$gateRoot = Split-Path $PSScriptRoot -Parent
$gateFonts = [System.Drawing.Text.PrivateFontCollection]::new()
$gateFonts.AddFontFile((Join-Path $gateRoot 'App/Assets/Fonts/MPlus1p/MPLUS1p-Black.ttf'))
$gateBitmap = [System.Drawing.Bitmap]::new(2048, 344)
$gateGraphics = [System.Drawing.Graphics]::FromImage($gateBitmap)
$gateBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 247, 222))
$gateFormat = [System.Drawing.StringFormat]::new()
$gateFormat.Alignment = [System.Drawing.StringAlignment]::Center
$gateFormat.LineAlignment = [System.Drawing.StringAlignment]::Center
$gateFont = [System.Drawing.Font]::new($gateFonts.Families[0], 124, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$gateSubFont = [System.Drawing.Font]::new($gateFonts.Families[0], 45, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
try {
    $gateGraphics.Clear([System.Drawing.Color]::FromArgb(24, 62, 55))
    $gateGraphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $gateGraphics.DrawString('また来てください', $gateFont, $gateBrush, [System.Drawing.RectangleF]::new(30, 30, 1988, 180), $gateFormat)
    $gateGraphics.DrawString('THANK YOU FOR VISITING  /  SEE YOU AGAIN', $gateSubFont, $gateBrush, [System.Drawing.RectangleF]::new(30, 225, 1988, 80), $gateFormat)
    $gateBitmap.Save((Join-Path $gateRoot 'App/Assets/Models/Title/Exit/Farewell.png'), [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    foreach ($gateResource in @($gateSubFont, $gateFont, $gateFormat, $gateBrush, $gateGraphics, $gateBitmap, $gateFonts)) { $gateResource.Dispose() }
}

