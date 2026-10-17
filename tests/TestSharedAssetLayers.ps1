param([Parameter(Mandatory = $true)][string]$WorkRoot)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$fixture = Join-Path ([IO.Path]::GetFullPath($WorkRoot)) ('shared layers-' + [Guid]::NewGuid().ToString('N'))
$game = Join-Path $fixture 'Game'
$shared = Join-Path $fixture 'Engine/Content'
$output = Join-Path $fixture 'Output'
$metadata = '{"id":"11111111111111111111111111111111"}'
. (Join-Path $repoRoot 'scripts/BuildContentSnapshot.ps1')

function Write-Fixture([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path $Path -Parent) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Assert-File([string]$Path, [string]$Expected) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf) -or [IO.File]::ReadAllText($Path) -cne $Expected) {
        throw "Missing or incorrect content: $Path"
    }
}

function Sync-Fixture {
    & (Join-Path $repoRoot 'scripts/SyncBuildContent.ps1') -Content $game -Destination $output
}

Write-Fixture (Join-Path $game 'WP1.project.json') '{"engineRoot":"../Engine"}'
Write-Fixture (Join-Path $game 'Assets/Project.json') '{}'
Write-Fixture (Join-Path $shared 'Shaders/Mesh.hlsl') 'shared shader'
Write-Fixture (Join-Path $shared 'Shaders/Mesh.hlsl.meta') $metadata
Write-Fixture (Join-Path $shared 'Assets/Fonts/Test.ttf') 'shared font'
Write-Fixture (Join-Path $shared 'Assets/Fonts/LICENSE.txt') 'font license'
Sync-Fixture
Assert-File (Join-Path $output 'Shaders/Mesh.hlsl') 'shared shader'
Assert-File (Join-Path $output 'Shaders/Mesh.hlsl.meta') $metadata
Assert-File (Join-Path $output 'Assets/Fonts/Test.ttf') 'shared font'
Assert-File (Join-Path $output 'Assets/Fonts/LICENSE.txt') 'font license'

# 正本の更新とゲーム側の上書き、上書き削除後の復帰を検証する。
Write-Fixture (Join-Path $shared 'Shaders/Mesh.hlsl') 'updated shared shader'
Sync-Fixture
Assert-File (Join-Path $output 'Shaders/Mesh.hlsl') 'updated shared shader'
Write-Fixture (Join-Path $game 'Shaders/Mesh.hlsl') 'game override'
Write-Fixture (Join-Path $game 'Shaders/Mesh.hlsl.meta') $metadata
Write-Fixture (Join-Path $game 'Shaders/Custom.hlsl') 'game only'
Sync-Fixture
Assert-File (Join-Path $output 'Shaders/Mesh.hlsl') 'game override'
Assert-File (Join-Path $output 'Shaders/Custom.hlsl') 'game only'
Assert-File (Join-Path $shared 'Shaders/Mesh.hlsl') 'updated shared shader'
Remove-Item -LiteralPath (Join-Path $game 'Shaders/Mesh.hlsl'), (Join-Path $game 'Shaders/Mesh.hlsl.meta')
Sync-Fixture
Assert-File (Join-Path $output 'Shaders/Mesh.hlsl') 'updated shared shader'

# 配布用スナップショットは共通素材を実体化し、新規ゲームのソースには複製しない。
Write-Fixture (Join-Path $game 'Assets/Scripts/Bin/Ignore.dll') 'generated module'
Write-Fixture (Join-Path $shared 'Shaders/Compiled/Ignore.bin') 'generated shader'
$snapshot = Join-Path $fixture 'Snapshot'
$clone = Join-Path $fixture 'Clone'
Copy-BuildContentSnapshot -Content $game -Destination $snapshot
Copy-BuildContentSnapshot -Content $game -Destination $clone -IncludeShared $false
Assert-File (Join-Path $snapshot 'Shaders/Mesh.hlsl') 'updated shared shader'
Assert-File (Join-Path $snapshot 'Assets/Fonts/LICENSE.txt') 'font license'
Assert-File (Join-Path $clone 'Shaders/Custom.hlsl') 'game only'
foreach ($unexpected in @('Shaders/Mesh.hlsl', 'Assets/Fonts/Test.ttf', 'Assets/Scripts/Bin/Ignore.dll')) {
    if (Test-Path -LiteralPath (Join-Path $clone $unexpected)) { throw "Source clone contains $unexpected" }
}
foreach ($unexpected in @('Assets/Scripts/Bin/Ignore.dll', 'Shaders/Compiled/Ignore.bin', 'WP1.project.json')) {
    if (Test-Path -LiteralPath (Join-Path $snapshot $unexpected)) { throw "Snapshot contains $unexpected" }
}
Write-Fixture (Join-Path $shared 'Shaders/Mesh.hlsl') 'changed after snapshot'
Assert-File (Join-Path $snapshot 'Shaders/Mesh.hlsl') 'updated shared shader'

# 不正な上書きIDは既存の出力を変更する前に拒否する。
Write-Fixture (Join-Path $game 'Shaders/Mesh.hlsl') 'invalid override'
Write-Fixture (Join-Path $game 'Shaders/Mesh.hlsl.meta') '{"id":"22222222222222222222222222222222"}'
$rejected = $false
try { Sync-Fixture } catch { $rejected = $_.Exception.Message -like '*preserve asset ID*' }
if (!$rejected) { throw 'Synchronization accepted an incompatible shared asset ID.' }
Assert-File (Join-Path $output 'Shaders/Mesh.hlsl') 'updated shared shader'
$rejected = $false
try { Copy-BuildContentSnapshot -Content $game -Destination (Join-Path $fixture 'Invalid') } catch {
    $rejected = $_.Exception.Message -like '*preserve asset ID*'
}
if (!$rejected) { throw 'Snapshot accepted an incompatible shared asset ID.' }

$project = Join-Path $fixture 'NewGame'
& (Join-Path $repoRoot 'scripts/NewGameProject.ps1') -Destination $project -Name SharedTest
if (Test-Path -LiteralPath (Join-Path $project 'Shaders/Mesh.hlsl')) { throw 'New project duplicated a shared shader.' }
if (Test-Path -LiteralPath (Join-Path $project 'Assets/Fonts/FiraMono/FiraMono-Regular.ttf')) { throw 'New project duplicated a shared font.' }
if ((Get-SharedContentRoot -Content $project) -ne (Join-Path $repoRoot 'Content')) { throw 'New project lost its engine reference.' }
if (!(Test-Path -LiteralPath (Join-Path $project 'Shaders/TitleCityEffect.hlsl'))) { throw 'New project lost game-specific shaders.' }

# コピー中の共通素材の変更も、ゲーム素材と同じく検出して配布を中止する。
Remove-Item -LiteralPath (Join-Path $game 'Shaders/Mesh.hlsl'), (Join-Path $game 'Shaders/Mesh.hlsl.meta')
function Copy-Item {
    param([string]$LiteralPath, [string]$Destination)
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination
    if ($LiteralPath -eq (Join-Path $shared 'Shaders/Mesh.hlsl')) {
        Write-Fixture $LiteralPath 'concurrent shared edit'
    }
}
$rejected = $false
try { Copy-BuildContentSnapshot -Content $game -Destination (Join-Path $fixture 'Interrupted') } catch {
    $rejected = $_.Exception.Message -like '*Content changed*'
}
if (!$rejected) { throw 'Snapshot missed a concurrent shared-content edit.' }
Write-Output 'PASS: shared layers, overrides, IDs, immutable snapshots, concurrent edits, fonts/licenses and new projects.'
