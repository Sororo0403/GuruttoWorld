$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../scripts/BuildContentSnapshot.ps1')
$testRoot=Join-Path (Split-Path $PSScriptRoot -Parent) ('generated/tests/build-snapshot-'+[Guid]::NewGuid().ToString('N'))
$source=Join-Path $testRoot 'Content'
$destination=Join-Path $testRoot 'Package'
New-Item -ItemType Directory -Path (Join-Path $source 'Assets/Scenes'),(Join-Path $source 'Shaders') -Force | Out-Null
$scene=Join-Path $source 'Assets/Scenes/Main.json'
Set-Content -LiteralPath $scene -Value 'original'
Set-Content -LiteralPath (Join-Path $source 'Shaders/Test.hlsl') -Value 'shader'
Set-Content -LiteralPath "$scene.tmp" -Value 'temporary save'
Copy-BuildContentSnapshot -Content $source -Destination $destination
if((Get-Content -LiteralPath (Join-Path $destination 'Assets/Scenes/Main.json') -Raw).Trim() -ne 'original') {throw 'Snapshot did not preserve scene.'}
if(Test-Path -LiteralPath (Join-Path $destination 'Assets/Scenes/Main.json.tmp')) {throw 'Snapshot included temporary saves.'}
Set-Content -LiteralPath $scene -Value 'later edit'
if((Get-Content -LiteralPath (Join-Path $destination 'Assets/Scenes/Main.json') -Raw).Trim() -ne 'original') {throw 'Snapshot changed with live Content.'}
# Inject a source edit immediately after copying an isolated test fixture.
function Copy-Item {
    param([string]$LiteralPath,[string]$Destination)
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination
    if($LiteralPath -like '*.json') {Set-Content -LiteralPath $LiteralPath -Value 'concurrent edit'}
}
$failed=$false
try {Copy-BuildContentSnapshot -Content $source -Destination (Join-Path $testRoot 'Interrupted')} catch {$failed=$true}
if(!$failed) {throw 'Concurrent source edit was not detected.'}
Write-Output 'PASS: immutable Content snapshot, temporary-save exclusion and concurrent-edit rejection'
