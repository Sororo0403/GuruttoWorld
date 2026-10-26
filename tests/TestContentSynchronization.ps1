param(
    [string]$WorkRoot,
    [ValidateSet('Debug', 'Development', 'Release')][string[]]$Configurations = @('Debug', 'Development', 'Release')
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'ProjectToolTestSupport.ps1')
if (!$WorkRoot) { $WorkRoot = Join-Path $repoRoot 'generated/tests' }
$testRoot = Join-Path ([IO.Path]::GetFullPath($WorkRoot)) ('content sync-' + [Guid]::NewGuid().ToString('N'))
$content = Join-Path $testRoot 'Game'
$fallback = Join-Path $testRoot 'Player'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (!$installation) { throw 'MSBuild is required.' }
$msbuild = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'

function Write-Fixture([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path $Path -Parent) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Assert-File([string]$Path, [string]$Expected) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf) -or [IO.File]::ReadAllText($Path) -cne $Expected) {
        throw "Missing or incorrect content: $Path"
    }
}

function Invoke-ContentCopy([string]$Configuration, [string]$Output) {
    & $msbuild (Join-Path $repoRoot 'Engine/build/Content.targets') '/t:CopyGameContent' `
        "/p:Configuration=$Configuration" '/p:SkipGameScripts=true' '/p:PreferredToolArchitecture=x64' `
        "/p:GameContentRoot=$content/" "/p:OutDir=$Output/" `
        '/nr:false' '/v:minimal' '/nologo'
    if ($LASTEXITCODE -ne 0) { throw "Content copy failed: $Configuration" }
}

$licenses = @{
    'LICENSE.txt' = 'Engine/externals/assimp/LICENSE.txt'
    'zlib-LICENSE.txt' = 'Engine/externals/assimp/zlib-LICENSE.txt'
    'nlohmann-json-LICENSE.txt' = 'Engine/externals/nlohmann/LICENSE.MIT'
    'DearImGui-LICENSE.txt' = 'Engine/externals/imgui/LICENSE.txt'
    'Jolt-LICENSE.txt' = 'generated/dependencies/jolt-src/LICENSE'
}

foreach ($configuration in $Configurations) {
    $output = Join-Path $testRoot $configuration
    New-Item -ItemType Directory -Path (Join-Path $content 'Shaders'), (Join-Path $fallback 'Shaders') -Force | Out-Null
    $metadata = '{"id":"12345678123456781234567812345678","version":1}'
    Write-Fixture (Join-Path $content 'Assets/Old.json') '{"saved":true}'
    Write-Fixture (Join-Path $content 'Assets/Old.json.meta') $metadata
    Write-Fixture (Join-Path $content 'Shaders/Old.hlsl') 'old shader'
    Write-Fixture (Join-Path $content 'Shaders/Old.hlsl.meta') '{"id":"abcdef12abcdef12abcdef12abcdef12","version":1}'
    Write-Fixture (Join-Path $fallback 'Assets/Shared.txt') 'fallback'
    Write-Fixture (Join-Path $fallback 'Assets/Player.txt') 'player'
    Write-Fixture (Join-Path $content 'Assets/Shared.txt') 'game'
    Write-Fixture (Join-Path $content 'Assets/Scripts/Bin/Source.dll') 'do not copy'
    Write-Fixture (Join-Path $content 'Assets/Unsaved.json.tmp') 'do not copy'
    Write-Fixture (Join-Path $output 'Assets/Legacy.json') 'stale before first synchronization'
    Write-Fixture (Join-Path $output 'Assets/Legacy.json.meta') $metadata
    Write-Fixture (Join-Path $output 'Assets/Scripts/Bin/Keep.dll') 'compiled module'
    Write-Fixture (Join-Path $output 'Shaders/Compiled/Keep.bin') 'compiled shader'
    Write-Fixture (Join-Path $output 'App.exe') 'outside managed folders'

    Invoke-ContentCopy $configuration $output
    Assert-File (Join-Path $output 'Assets/Shared.txt') 'game'
    if (Test-Path -LiteralPath (Join-Path $output 'Assets/Player.txt')) { throw 'Build copied another owner assets.' }
    if (!(Test-Path -LiteralPath (Join-Path $output 'Engine/Shaders/Mesh.hlsl'))) { throw 'Engine shaders are missing.' }
    if (Test-Path -LiteralPath (Join-Path $output 'Assets/Fonts')) { throw 'Player contains Editor fonts.' }
    foreach ($relative in @('Assets/Legacy.json', 'Assets/Legacy.json.meta', 'Assets/Unsaved.json.tmp', 'Assets/Scripts/Bin/Source.dll')) {
        if (Test-Path -LiteralPath (Join-Path $output $relative)) { throw "Unexpected copied file: $relative" }
    }
    $timestamp = (Get-Item -LiteralPath (Join-Path $output 'Assets/Old.json')).LastWriteTimeUtc
    Invoke-ContentCopy $configuration $output
    if ((Get-Item -LiteralPath (Join-Path $output 'Assets/Old.json')).LastWriteTimeUtc -ne $timestamp) {
        throw 'Unchanged content was rewritten.'
    }

    # 退避先を含めた絶対パスは、テスト専用ディレクトリの内側に限定する。
    foreach ($relative in @('Assets/Old.json', 'Assets/Old.json.meta', 'Shaders/Old.hlsl', 'Shaders/Old.hlsl.meta')) {
        $old = [IO.Path]::GetFullPath((Join-Path $content $relative))
        $new = [IO.Path]::GetFullPath($old.Replace('Old.', 'New.'))
        if (!$old.StartsWith($testRoot + '\') -or !$new.StartsWith($testRoot + '\')) { throw 'Invalid fixture path.' }
        Move-Item -LiteralPath $old -Destination $new
    }
    Invoke-ContentCopy $configuration $output
    foreach ($relative in @('Assets/Old.json', 'Assets/Old.json.meta', 'Shaders/Old.hlsl', 'Shaders/Old.hlsl.meta')) {
        if (Test-Path -LiteralPath (Join-Path $output $relative)) { throw "Renamed file remains: $relative" }
    }
    Assert-File (Join-Path $output 'Assets/New.json.meta') $metadata
    Assert-File (Join-Path $output 'Shaders/New.hlsl') 'old shader'
    Assert-File (Join-Path $output 'Assets/Scripts/Bin/Keep.dll') 'compiled module'
    Assert-File (Join-Path $output 'Shaders/Compiled/Keep.bin') 'compiled shader'
    Assert-File (Join-Path $output 'App.exe') 'outside managed folders'
    foreach ($name in $licenses.Keys) {
        $source = Join-Path $repoRoot $licenses[$name]
        $copied = Join-Path $output "Licenses/$name"
        if (!(Test-Path -LiteralPath $copied) -or
            (Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $copied).Hash) {
            throw "Missing or stale license: $name"
        }
    }
    foreach ($relative in @('Assets/New.json', 'Assets/New.json.meta', 'Shaders/New.hlsl', 'Shaders/New.hlsl.meta')) {
        $path = [IO.Path]::GetFullPath((Join-Path $content $relative))
        if (!$path.StartsWith($testRoot + '\')) { throw 'Invalid fixture path.' }
        Remove-Item -LiteralPath $path
    }
    Invoke-ContentCopy $configuration $output
    if (Test-Path -LiteralPath (Join-Path $output 'Assets/New.json')) { throw 'Deleted asset remains.' }
    if (Test-Path -LiteralPath (Join-Path $output 'Shaders/New.hlsl')) { throw 'Deleted shader remains.' }
}

# コピー先の指定間違いで素材の正本を削除しないことも実プロセスで確認する。
$protected = Join-Path $content 'Assets/Shared.txt'
foreach ($unsafeOutput in @($content, (Join-Path $content 'Assets/Output'))) {
    try {
        $ErrorActionPreference = 'Continue'
        & (Get-ProjectTool) sync-content -Engine $repoRoot `
            -Content $content -Destination $unsafeOutput *> (Join-Path $testRoot 'overlap.log')
        $overlapExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = 'Stop'
    }
    if ($overlapExit -eq 0) { throw 'Overlapping content destination was accepted.' }
    Assert-File $protected 'game'
}

# Windowsで同じ場所を表すUnicodeの大小文字も、重複出力として拒否します。
$unicodeContent = Join-Path $testRoot ('Unicode' + [char]0x00c4)
$unicodeOutput = Join-Path $testRoot ('Unicode' + [char]0x00e4)
$unicodeProtected = Join-Path $unicodeContent 'Assets/Keep.txt'
Write-Fixture $unicodeProtected 'protected source'
try {
    $ErrorActionPreference = 'Continue'
    & (Get-ProjectTool) sync-content -Engine $repoRoot -Content $unicodeContent -Destination $unicodeOutput `
        *> (Join-Path $testRoot 'unicode-overlap.log')
    $unicodeExit = $LASTEXITCODE
} finally { $ErrorActionPreference = 'Stop' }
if ($unicodeExit -eq 0) { throw 'Unicode case alias of the source was accepted as output.' }
Assert-File $unicodeProtected 'protected source'

# ライセンス不足を無視する条件が再導入されないよう、欠損したコピー元で検査する。
$licenseFixture = Join-Path $testRoot 'missing-license'
$licenseTargets = Join-Path $licenseFixture 'Engine/build/ThirdPartyLicenses.targets'
New-Item -ItemType Directory -Path (Split-Path $licenseTargets -Parent) -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'Engine/build/ThirdPartyLicenses.targets') -Destination $licenseTargets
foreach ($name in $licenses.Keys) {
    if ($name -eq 'nlohmann-json-LICENSE.txt') { continue }
    Write-Fixture (Join-Path $licenseFixture $licenses[$name]) 'test license'
}
& $msbuild $licenseTargets '/t:CopyThirdPartyLicenses' "/p:OutDir=$licenseFixture/output/" '/p:PreferredToolArchitecture=x64' `
    '/nr:false' '/v:minimal' '/nologo' *> (Join-Path $testRoot 'missing-license.log')
if ($LASTEXITCODE -eq 0) { throw 'Missing third-party license was accepted.' }
Write-Output "PASS: rename/delete synchronization, legacy cleanup, DLL preservation, source protection and licenses: $($Configurations -join ', ')"
