param(
    [switch]$Worker,
    [switch]$ExpectTimeout,
    [string]$Fixture,
    [string]$Name = 'owner'
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $taskRoot 'scripts/ScriptBuildSession.ps1')

if ($Worker) {
    $session = $null
    try {
        [IO.File]::WriteAllText((Join-Path $Fixture "$Name.started"), 'started')
        try {
            $session = Enter-ScriptBuildSession -OutputContent $Fixture -Configuration Release -TimeoutSeconds 1
        } catch {
            if ($ExpectTimeout -and $_.Exception.Message -like 'Timed out*') { exit 0 }
            throw
        }
        if ($ExpectTimeout) { throw 'Concurrent build unexpectedly entered the publication session.' }
        [IO.File]::WriteAllText((Join-Path $Fixture "$Name.entered"), 'entered')
        $watch = [Diagnostics.Stopwatch]::StartNew()
        while (!(Test-Path -LiteralPath (Join-Path $Fixture "$Name.release"))) {
            if ($watch.Elapsed.TotalSeconds -gt 20) { throw 'Test release gate timed out.' }
            Start-Sleep -Milliseconds 20
        }
        Publish-ScriptManifest -Path (Join-Path $Fixture 'module.json') -Json '{"version":1,"file":"owner.dll"}'
    } finally {
        Exit-ScriptBuildSession -Session $session
    }
    exit 0
}

$Fixture = Join-Path $taskRoot ('generated/tests/script-publication/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Fixture -Force | Out-Null
$taskShell = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$taskChildren = @()

function Start-Worker {
    param([string]$WorkerName, [switch]$Timeout)
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $PSCommandPath + '"'),
        '-Worker', '-Fixture', ('"' + $Fixture + '"'), '-Name', $WorkerName)
    if ($Timeout) { $arguments += '-ExpectTimeout' }
    $process = Start-Process -FilePath $taskShell -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $Fixture "$WorkerName.out") `
        -RedirectStandardError (Join-Path $Fixture "$WorkerName.err")
    $null = $process.Handle
    return $process
}

function Wait-Marker {
    param([string]$Marker)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (!(Test-Path -LiteralPath (Join-Path $Fixture $Marker))) {
        if ($watch.Elapsed.TotalSeconds -gt 10) { throw "Missing worker marker: $Marker" }
        Start-Sleep -Milliseconds 20
    }
}

try {
    $owner = Start-Worker -WorkerName owner
    $taskChildren += $owner
    Wait-Marker 'owner.entered'
    $contender = Start-Worker -WorkerName contender -Timeout
    $taskChildren += $contender
    if (!$contender.WaitForExit(10000) -or $contender.ExitCode -ne 0) { throw 'Output publication was not serialized.' }
    if (Test-Path -LiteralPath (Join-Path $Fixture 'contender.entered')) { throw 'Contender entered a held session.' }
    [IO.File]::WriteAllText((Join-Path $Fixture 'owner.release'), 'release')
    if (!$owner.WaitForExit(10000) -or $owner.ExitCode -ne 0) { throw 'Owner publication failed.' }

    $manifest = Join-Path $Fixture 'module.json'
    $before = [IO.File]::ReadAllText($manifest)
    $locked = [IO.File]::Open($manifest, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $rejected = $false
    try {
        try { Publish-ScriptManifest -Path $manifest -Json '{"file":"broken.dll"}' } catch { $rejected = $true }
    } finally {
        $locked.Dispose()
    }
    if (!$rejected -or [IO.File]::ReadAllText($manifest) -cne $before) { throw 'Failed publication changed the previous manifest.' }
    if (@(Get-ChildItem -LiteralPath $Fixture -Filter '*.tmp').Count) { throw 'Failed publication leaked temporary files.' }

    $abandoned = Start-Worker -WorkerName abandoned
    $taskChildren += $abandoned
    Wait-Marker 'abandoned.entered'
    $abandoned.Kill()
    $abandoned.WaitForExit()
    $recovered = Enter-ScriptBuildSession -OutputContent ($Fixture + '/.') -Configuration Release -TimeoutSeconds 1
    try {
        Publish-ScriptManifest -Path $manifest -Json '{"file":"recovered.dll"}'
    } finally {
        Exit-ScriptBuildSession -Session $recovered
    }
    if (([IO.File]::ReadAllText($manifest) | ConvertFrom-Json).file -cne 'recovered.dll') { throw 'Abandoned session recovery failed.' }
    Write-Output 'PASS: process exclusion, failed atomic publication, temporary cleanup and abandoned-session recovery'
} finally {
    foreach ($child in $taskChildren) {
        if (!$child.HasExited) { $child.Kill(); $child.WaitForExit() }
        $child.Dispose()
    }
}
