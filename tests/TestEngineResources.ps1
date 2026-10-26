param([string]$WorkRoot)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
if (!$WorkRoot) { $WorkRoot = Join-Path $repoRoot 'generated/tests' }
$fixture = Join-Path ([IO.Path]::GetFullPath($WorkRoot)) ('engine resources-' + [Guid]::NewGuid().ToString('N'))
$game = Join-Path $fixture 'Game'
$engine = Join-Path $fixture 'SDK'
$output = Join-Path $fixture 'Output'
. (Join-Path $PSScriptRoot 'ProjectToolTestSupport.ps1')
. (Join-Path $PSScriptRoot 'SnapshotTestSupport.ps1')
function Write-Fixture([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path $Path -Parent) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Assert-File([string]$Path, [string]$Expected) {
    if (!(Test-Path -LiteralPath $Path) -or [IO.File]::ReadAllText($Path) -cne $Expected) { throw "Incorrect file: $Path" }
}
Write-Fixture (Join-Path $game 'WP1.project.json') '{"engineRoot":"../SDK"}'
Write-Fixture (Join-Path $game 'Assets/Project.json') '{}'
Write-Fixture (Join-Path $engine 'Engine/Shaders/Mesh.hlsl') 'engine shader'
Write-Fixture (Join-Path $engine 'Editor/Resources/Fonts/Editor.ttf') 'editor font'
Write-Fixture (Join-Path $engine 'Engine/Shaders/Compiled/Ignore.bin') 'generated cache'
Write-Fixture (Join-Path $game 'Shaders/Mesh.hlsl') 'game shader'
& (Get-ProjectTool) sync-content -Engine $engine -Content $game -Destination $output
if ($LASTEXITCODE -ne 0) { throw 'Content synchronization failed.' }
Assert-File (Join-Path $output 'Engine/Shaders/Mesh.hlsl') 'engine shader'
Assert-File (Join-Path $output 'Shaders/Mesh.hlsl') 'game shader'
if (Test-Path -LiteralPath (Join-Path $output 'Assets/Fonts')) { throw 'Editor fonts leaked into player.' }
$snapshot = Join-Path $fixture 'Snapshot'
$clone = Join-Path $fixture 'Clone'
Copy-BuildContentSnapshot -Content $game -Destination $snapshot
Copy-BuildContentSnapshot -Content $game -Destination $clone -IncludeEngineResources $false
Assert-File (Join-Path $snapshot 'Engine/Shaders/Mesh.hlsl') 'engine shader'
Assert-File (Join-Path $clone 'Shaders/Mesh.hlsl') 'game shader'
foreach ($unexpected in @('Engine/Shaders/Compiled/Ignore.bin', 'Editor', 'WP1.project.json')) {
    if (Test-Path -LiteralPath (Join-Path $snapshot $unexpected)) { throw "Snapshot contains $unexpected" }
}
if (Test-Path -LiteralPath (Join-Path $clone 'Engine')) { throw 'Source clone copied engine resources.' }
Write-Fixture (Join-Path $engine 'Engine/Shaders/Mesh.hlsl') 'updated engine shader'
Assert-File (Join-Path $snapshot 'Engine/Shaders/Mesh.hlsl') 'engine shader'
& (Get-ProjectTool) sync-content -Engine $engine -Content $game -Destination $output
if ($LASTEXITCODE -ne 0) { throw 'Content synchronization failed.' }
Assert-File (Join-Path $output 'Engine/Shaders/Mesh.hlsl') 'updated engine shader'
Assert-File (Join-Path $output 'Shaders/Mesh.hlsl') 'game shader'
$project = Join-Path $fixture 'NewGame'
& (Get-ProjectTool) new-project -Engine $repoRoot -Destination $project -Name ResourceTest
if ($LASTEXITCODE -ne 0) { throw 'Project creation failed.' }
foreach ($unexpected in @('Engine', 'Editor', 'Assets/Fonts', 'Shaders/Mesh.hlsl')) {
    if (Test-Path -LiteralPath (Join-Path $project $unexpected)) { throw "New game contains $unexpected" }
}
if (!(Test-Path -LiteralPath (Join-Path $project 'Assets/Scenes/Main.json'))) { throw 'Missing template scene.' }
Assert-SnapshotConcurrentChange -Content $game -Destination (Join-Path $fixture 'Interrupted') `
    -MutatedFile (Join-Path $engine 'Engine/Shaders/Mesh.hlsl') -TriggerRelative 'Engine/Shaders/Mesh.hlsl'
Write-Output 'PASS: resource ownership, independent shader names, Editor isolation, snapshots and new projects.'
