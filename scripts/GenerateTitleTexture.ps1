# タイトル文字を通常のスプライトとして表示するための画像を生成します。
# ビルド時の実行は不要です。生成済み PNG を Assets に保存します。
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$titleBitmap = [System.Drawing.Bitmap]::new(960, 320)
$titleGraphics = [System.Drawing.Graphics]::FromImage($titleBitmap)
$titleFormat = [System.Drawing.StringFormat]::new()
$titleFormat.Alignment = [System.Drawing.StringAlignment]::Center
$titleFormat.LineAlignment = [System.Drawing.StringAlignment]::Center
$titleFont = [System.Drawing.Font]::new('Segoe UI', 88, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$startFont = [System.Drawing.Font]::new('Segoe UI', 28, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$helpFont = [System.Drawing.Font]::new('Segoe UI', 20, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$titleBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(240, 247, 255))
$startBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(114, 203, 255))
$helpBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(175, 190, 211))
try {
    $titleGraphics.Clear([System.Drawing.Color]::FromArgb(10, 18, 38))
    $titleGraphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $titleGraphics.DrawString('WP1', $titleFont, $titleBrush, [System.Drawing.RectangleF]::new(0, 18, 960, 132), $titleFormat)
    $titleGraphics.DrawString('PRESS ENTER TO START', $startFont, $startBrush, [System.Drawing.RectangleF]::new(0, 172, 960, 58), $titleFormat)
    $titleGraphics.DrawString('Escape in game: return to title', $helpFont, $helpBrush, [System.Drawing.RectangleF]::new(0, 244, 960, 44), $titleFormat)
    $titleOutput = Join-Path (Split-Path $PSScriptRoot -Parent) 'App/Assets/Textures/Title.png'
    $titleBitmap.Save($titleOutput, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    foreach ($titleDisposable in @($helpBrush, $startBrush, $titleBrush, $helpFont, $startFont, $titleFont, $titleFormat, $titleGraphics, $titleBitmap)) {
        $titleDisposable.Dispose()
    }
}
