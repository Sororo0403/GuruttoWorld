# 同梱フォントからタイトル UI の部品を1枚の透明アトラスに生成します。
# 通常ビルドは生成済み PNG を使用するため、このスクリプトの実行は不要です。
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$titleRoot = Split-Path $PSScriptRoot -Parent
$titleFonts = [System.Drawing.Text.PrivateFontCollection]::new()
$titleFonts.AddFontFile((Join-Path $titleRoot 'App/Assets/Fonts/MPlus1p/MPLUS1p-Black.ttf'))
$titleBitmap = [System.Drawing.Bitmap]::new(2048, 2048)
$titleGraphics = [System.Drawing.Graphics]::FromImage($titleBitmap)
$titleWhite = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::White)
$titleInk = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(10, 16, 24))
$titlePink = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 53, 139))
$titleCream = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 248, 225))
$titleMint = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(81, 242, 192), 12)

function Draw-TitleText {
    param([string]$Text, [float]$X, [float]$Y, [float]$Width, [float]$Height,
        [System.Drawing.Brush]$Brush, [bool]$Stretch = $false)
    $titlePath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $titleMatrix = [System.Drawing.Drawing2D.Matrix]::new()
    $titleFormat = [System.Drawing.StringFormat]::GenericTypographic
    try {
        $titlePath.AddString($Text, $titleFonts.Families[0], 0, 100,
            [System.Drawing.PointF]::new(0, 0), $titleFormat)
        $titleBounds = $titlePath.GetBounds()
        $titleScaleX = $Width / $titleBounds.Width
        $titleScaleY = $Height / $titleBounds.Height
        if (-not $Stretch) { $titleScaleX = $titleScaleY = [Math]::Min($titleScaleX, $titleScaleY) }
        $titleMatrix.Translate(-$titleBounds.X, -$titleBounds.Y)
        $titlePath.Transform($titleMatrix)
        $titleMatrix.Reset()
        $titleMatrix.Scale($titleScaleX, $titleScaleY)
        $titlePath.Transform($titleMatrix)
        $titleMatrix.Reset()
        $titleMatrix.Translate($X, $Y)
        $titlePath.Transform($titleMatrix)
        $titleGraphics.FillPath($Brush, $titlePath)
    }
    finally { $titleFormat.Dispose(); $titleMatrix.Dispose(); $titlePath.Dispose() }
}

try {
    $titleGraphics.Clear([System.Drawing.Color]::Transparent)
    $titleGraphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $titleGraphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    # ロゴ領域 (0,0)-(1024,480)。背景が透ける外形で街の見える面積を確保します。
    $titleGraphics.FillPolygon($titleInk, [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(20,70), [System.Drawing.PointF]::new(1000,5),
        [System.Drawing.PointF]::new(956,200), [System.Drawing.PointF]::new(1020,170),
        [System.Drawing.PointF]::new(942,446), [System.Drawing.PointF]::new(8,478),
        [System.Drawing.PointF]::new(68,248), [System.Drawing.PointF]::new(0,265)))
    Draw-TitleText 'NEW' 130 38 695 170 $titlePink $true
    Draw-TitleText 'WORLD' 52 235 855 172 $titleCream $true
    $titleGraphics.DrawBezier($titleMint, 26, 400, 190, 495, 815, 405, 988, 306)
    # 汎用の白い帯。ランタイムで黒・ピンク・クリームに着色します。
    $titleGraphics.FillPolygon($titleWhite, [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(1064,8), [System.Drawing.PointF]::new(2016,8),
        [System.Drawing.PointF]::new(1976,192), [System.Drawing.PointF]::new(1024,192)))
    # 矢印 (1024,224)-(1152,384)。
    $titleGraphics.FillPolygon($titleWhite, [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(1034,234), [System.Drawing.PointF]::new(1140,304),
        [System.Drawing.PointF]::new(1034,374), [System.Drawing.PointF]::new(1066,304)))
    Draw-TitleText 'はじめる' 12 524 780 68 $titleWhite
    Draw-TitleText '設定' 12 620 780 62 $titleWhite
    Draw-TitleText '終了' 12 700 780 62 $titleWhite
    Draw-TitleText 'まだ見ぬ世界へ' 12 780 920 42 $titleWhite
    Draw-TitleText '↑↓ 選択   Enter 決定' 12 844 920 42 $titleWhite
    Draw-TitleText '十字キー / 左スティック 選択   A 決定' 12 924 1380 42 $titleWhite
    Draw-TitleText '音量' 12 1036 780 60 $titleWhite
    Draw-TitleText '背景演出' 12 1116 780 60 $titleWhite
    Draw-TitleText '保存して戻る' 12 1196 780 60 $titleWhite
    Draw-TitleText 'ON' 12 1276 360 50 $titleWhite
    Draw-TitleText 'OFF' 412 1276 360 50 $titleWhite
    Draw-TitleText '↑↓ 選択  ←→ 変更  Enter 決定  Esc 取消' 12 1756 1900 42 $titleWhite
    Draw-TitleText '十字キー / 左スティック 変更  A 決定  B 取消' 12 1820 1900 42 $titleWhite
    Draw-TitleText '背景演出：カメラの動きと光の粒' 12 1884 1900 42 $titleWhite
    Draw-TitleText '保存できませんでした。もう一度お試しください' 12 1948 1900 42 $titleWhite
    for ($titleLevel = 0; $titleLevel -le 10; $titleLevel++) {
        Draw-TitleText (($titleLevel * 10).ToString() + ' %') 1036 (1036 + $titleLevel * 64) 360 44 $titleWhite
    }
    $titleOutput = Join-Path $titleRoot 'App/Assets/Textures/Title/UiAtlas.png'
    $titleBitmap.Save($titleOutput, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    foreach ($titleDisposable in @($titleMint, $titleCream, $titlePink, $titleInk, $titleWhite,
        $titleGraphics, $titleBitmap, $titleFonts)) { $titleDisposable.Dispose() }
}
