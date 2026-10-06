# 朝のタイトルUI。図形と同梱フォントから独自のロゴ・帯・文字部品を生成します。
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$morningRoot = Split-Path $PSScriptRoot -Parent
$morningOutput = Join-Path $morningRoot 'Content/Assets/Textures/Title/Morning'
New-Item -ItemType Directory -Force $morningOutput | Out-Null
$morningFonts = [System.Drawing.Text.PrivateFontCollection]::new()
$morningFonts.AddFontFile((Join-Path $morningRoot 'App/Assets/Fonts/MPlus1p/MPLUS1p-Black.ttf'))
$morningRed = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 255, 114, 74))
$morningWhite = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 255, 252, 244))
$morningInk = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 11, 27, 44))
$morningBlue = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 25, 103, 172))
$morningCyan = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 108, 229, 239))
function New-MorningImage {
    param([string]$Name,[int]$Width,[int]$Height,[scriptblock]$Draw)
    $morningBitmap=[System.Drawing.Bitmap]::new($Width,$Height)
    $morningGraphics=[System.Drawing.Graphics]::FromImage($morningBitmap)
    try {
        $morningGraphics.Clear([System.Drawing.Color]::Transparent)
        $morningGraphics.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        & $Draw $morningGraphics
        $morningBitmap.Save((Join-Path $morningOutput ($Name+'.png')),[System.Drawing.Imaging.ImageFormat]::Png)
    } finally {$morningGraphics.Dispose();$morningBitmap.Dispose()}
}
function Morning-Polygon {
    param($Graphics,$Brush,[float[]]$Coordinates)
    $morningPoints=for($i=0;$i -lt $Coordinates.Length;$i+=2){[System.Drawing.PointF]::new($Coordinates[$i],$Coordinates[$i+1])}
    $Graphics.FillPolygon($Brush,[System.Drawing.PointF[]]$morningPoints)
}
function Morning-Type {
    param($Graphics,[string]$Text,[float]$X,[float]$Y,[float]$Width,[float]$Height,$Brush,[float]$Angle=-6)
    $morningPath=[System.Drawing.Drawing2D.GraphicsPath]::new()
    $morningMatrix=[System.Drawing.Drawing2D.Matrix]::new()
    $morningPen=[System.Drawing.Pen]::new($morningInk.Color,15)
    $morningPen.LineJoin=[System.Drawing.Drawing2D.LineJoin]::Round
    try {
        $morningPath.AddString($Text,$morningFonts.Families[0],0,100,[System.Drawing.PointF]::new(0,0),[System.Drawing.StringFormat]::GenericTypographic)
        $morningBounds=$morningPath.GetBounds()
        $morningMatrix.Translate(-$morningBounds.X,-$morningBounds.Y);$morningPath.Transform($morningMatrix)
        $morningMatrix.Reset();$morningMatrix.Scale($Width/$morningBounds.Width,$Height/$morningBounds.Height);$morningPath.Transform($morningMatrix)
        $morningMatrix.Reset();$morningMatrix.Shear(-0.12,0);$morningMatrix.Rotate($Angle);$morningMatrix.Translate($X,$Y,[System.Drawing.Drawing2D.MatrixOrder]::Append);$morningPath.Transform($morningMatrix)
        $Graphics.DrawPath($morningPen,$morningPath);$Graphics.FillPath($Brush,$morningPath)
    } finally {$morningPen.Dispose();$morningMatrix.Dispose();$morningPath.Dispose()}
}
try {
    New-MorningImage 'Logo' 1120 600 {
        param($g)
        Morning-Polygon $g $morningRed @(114,400,1060,290,1005,456,20,573)
        Morning-Type $g 'ぐるっと' 153 101 827 174 $morningWhite -8
        Morning-Type $g 'ワールド' 89 295 977 200 $morningWhite -8
        Morning-Type $g 'GURUTTO WORLD' 186 526 724 33 $morningWhite -8
    }
    New-MorningImage 'StartBand' 1000 230 {
        param($g)
        Morning-Polygon $g $morningBlue @(5,72,941,0,991,170,56,229)
        Morning-Polygon $g $morningCyan @(107,13,958,46,919,173,45,152)
        Morning-Type $g 'PRESS ANY BUTTON' 90 80 816 54 $morningWhite -4
    }
    New-MorningImage 'Slash' 1280 720 {
        param($g)
        Morning-Polygon $g $morningRed @(0,642,1280,393,1280,460,0,720)
        Morning-Polygon $g $morningWhite @(0,606,1280,356,1280,367,0,618)
    }
    New-MorningImage 'Caption' 900 100 {
        param($g)
        Morning-Type $g 'HELLO, NEW DAY.' 24 45 834 36 $morningWhite -4
    }
} finally {
    foreach($morningResource in @($morningFonts,$morningRed,$morningWhite,$morningInk,$morningBlue,$morningCyan)){$morningResource.Dispose()}
}
