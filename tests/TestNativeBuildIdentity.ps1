param([ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Debug')

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskFixture = Join-Path $taskRoot ('generated/tests/native-identity-' + [Guid]::NewGuid().ToString('N'))
$taskSources = Join-Path $taskFixture 'Assets/Scripts'
New-Item -ItemType Directory -Path $taskSources -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fixtures/NativeModuleIdentity.cpp') -Destination (Join-Path $taskSources 'IdentityProbe.cpp')
$taskShell = Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'
& $taskShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $taskRoot 'scripts/BuildScripts.ps1') -Configuration $Configuration -Content $taskFixture -WorkRoot $taskFixture -RunId identity
if ($LASTEXITCODE -ne 0) { throw 'Native identity fixture compilation failed.' }
$taskModuleFolder = Join-Path $taskFixture "Assets/Scripts/Bin/$Configuration"
$taskManifest = Get-Content -LiteralPath (Join-Path $taskModuleFolder 'module.json') -Raw | ConvertFrom-Json
# DLL構築でSDKも更新されるため、照合するホストも同じライブラリーから構築します。
& (Join-Path $PSScriptRoot 'TestArchitectureContracts.ps1') -Configuration $Configuration -Baseline -Require project-native-module-identity -Module (Join-Path $taskModuleFolder $taskManifest.file)
Write-Output "PASS: $Configuration native module accepts matching identity and rejects mismatches before registration."
