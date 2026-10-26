param(
    [ValidateSet('Debug','Development','Release')][string]$Configuration='Debug',
    [string]$Content,
    [string]$SourceName='ValidationNativeMover.cpp'
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
if (!$Content) { $Content=Join-Path $repoRoot 'generated/tests/native-script-project/Content' }
$content=[IO.Path]::GetFullPath($Content)
$source=Join-Path (Join-Path $content 'Assets/Scripts') $SourceName
$manifest=Join-Path $content "Assets/Scripts/Bin/$Configuration/module.json"
if (!(Test-Path -LiteralPath $manifest)) { throw 'Build the native validation module first.' }
$before=[IO.File]::ReadAllText($manifest)
$code=[IO.File]::ReadAllText($source)
$runId='invalid-script-'+[Guid]::NewGuid().ToString('N')
. (Join-Path $PSScriptRoot 'ProjectToolTestSupport.ps1')
$tool=Get-ProjectTool
New-Item -ItemType Directory -Path (Join-Path $repoRoot 'generated/script-builds') -Force | Out-Null
try {
    [IO.File]::WriteAllText($source,$code+"`r`ninvalid_cpp_token_for_failure_test`r`n",[Text.UTF8Encoding]::new($false))
    try {
        $ErrorActionPreference = 'Continue'
        & $tool build-scripts -Engine $repoRoot -Content $content -WorkRoot $repoRoot -Configuration $Configuration -RunId $runId *> (Join-Path $repoRoot "generated/script-builds/$runId-test.log")
        $taskBuildExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = 'Stop' }
    if ($taskBuildExit -eq 0) { throw 'Invalid C++ was accepted.' }
    if ([IO.File]::ReadAllText($manifest) -cne $before) { throw 'Failed compilation changed the previous module manifest.' }
    $result=Get-Content -LiteralPath (Join-Path $repoRoot "generated/script-builds/$runId.json") -Raw | ConvertFrom-Json
    if ($result.success -or !$result.error -or !(Test-Path -LiteralPath $result.log)) { throw 'Compiler failure diagnostics were not published.' }
    Write-Output 'PASS: C++ compile failure publishes diagnostics and preserves previous module'
} finally { [IO.File]::WriteAllText($source,$code,[Text.UTF8Encoding]::new($false)) }
exit 0
