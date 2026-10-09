param([ValidateSet('Debug','Development','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$content=Join-Path $repoRoot 'generated/tests/native-script-project/Content'
$source=Join-Path $content 'Assets/Scripts/ValidationNativeMover.cpp'
$manifest=Join-Path $content "Assets/Scripts/Bin/$Configuration/module.json"
if (!(Test-Path -LiteralPath $manifest)) { throw 'Build the native validation module first.' }
$before=[IO.File]::ReadAllText($manifest)
$code=[IO.File]::ReadAllText($source)
$runId='invalid-script-'+[Guid]::NewGuid().ToString('N')
$shellPath=Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'
try {
    [IO.File]::WriteAllText($source,$code+"`r`ninvalid_cpp_token_for_failure_test`r`n",[Text.UTF8Encoding]::new($false))
    & $shellPath -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'scripts/BuildScripts.ps1') -Content $content -Configuration $Configuration -RunId $runId *> (Join-Path $repoRoot "generated/script-builds/$runId-test.log")
    if ($LASTEXITCODE -eq 0) { throw 'Invalid C++ was accepted.' }
    if ([IO.File]::ReadAllText($manifest) -cne $before) { throw 'Failed compilation changed the previous module manifest.' }
    $result=Get-Content -LiteralPath (Join-Path $repoRoot "generated/script-builds/$runId.json") -Raw | ConvertFrom-Json
    if ($result.success -or !$result.error -or !(Test-Path -LiteralPath $result.log)) { throw 'Compiler failure diagnostics were not published.' }
    Write-Output 'PASS: C++ compile failure publishes diagnostics and preserves previous module'
} finally { [IO.File]::WriteAllText($source,$code,[Text.UTF8Encoding]::new($false)) }
exit 0
