param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateRange(1, 20)][int]$Attempts = 3
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskExecutable = Join-Path $taskRoot "generated/tests/outputs/x64/$Configuration/ReviewRegressionValidation/ReviewRegressionValidation.exe"
if (-not (Test-Path -LiteralPath $taskExecutable)) { throw "Build ReviewRegressionValidation $Configuration before running this check." }
$taskLogDirectory = Join-Path $taskRoot "generated/tests/authoring-isolation/$Configuration-$PID-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $taskLogDirectory -Force | Out-Null
$taskPreviousModes = @(Get-ChildItem Env:WP1_*_ONLY)
$taskPreviousTemp = $env:TEMP
$taskPreviousTmp = $env:TMP
$taskTemporary = Join-Path $taskLogDirectory 'temp'
New-Item -ItemType Directory -Path $taskTemporary -Force | Out-Null
$env:TEMP = $taskTemporary
$env:TMP = $taskTemporary
foreach ($taskMode in $taskPreviousModes) { Remove-Item "Env:$($taskMode.Name)" }
$env:WP1_AUTHORING_ONLY = '1'
try {
    for ($taskAttempt = 1; $taskAttempt -le $Attempts; ++$taskAttempt) {
        $taskProcesses = @()
        try {
            foreach ($taskLabel in @('a', 'b')) {
                $taskProcesses += Start-Process -FilePath $taskExecutable -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru `
                    -RedirectStandardOutput (Join-Path $taskLogDirectory "$taskAttempt-$taskLabel-output.log") `
                    -RedirectStandardError (Join-Path $taskLogDirectory "$taskAttempt-$taskLabel-error.log")
            }
            foreach ($taskProcess in $taskProcesses) {
                if (-not $taskProcess.WaitForExit(60000)) { throw "Authoring validation timed out: $($taskProcess.Id)" }
                if ($taskProcess.ExitCode -ne 0) { throw "Concurrent authoring validation failed: $($taskProcess.ExitCode). Logs: $taskLogDirectory" }
            }
            $taskFolders = @()
            foreach ($taskLabel in @('a', 'b')) {
                $taskOutputLine = Get-Content (Join-Path $taskLogDirectory "$taskAttempt-$taskLabel-output.log") |
                    Where-Object { $_.StartsWith('Authoring output: ') } | Select-Object -First 1
                if (-not $taskOutputLine) { throw "Authoring artifact location is missing: $taskLogDirectory" }
                $taskFolder = Join-Path $taskRoot $taskOutputLine.Substring('Authoring output: '.Length)
                $taskFolders += $taskFolder
                $taskSettings = Get-Content (Join-Path $taskFolder 'preview-settings.json') -Raw | ConvertFrom-Json
                if ($taskSettings.volumeGain -ne 0) { throw "Concurrent settings save did not persist: $taskFolder" }
            }
            if ($taskFolders[0] -eq $taskFolders[1]) { throw "Authoring processes shared their generated files: $($taskFolders[0])" }
            Write-Output "PASS: $Configuration concurrent authoring validation $taskAttempt/$Attempts"
        } finally {
            foreach ($taskProcess in $taskProcesses) {
                if (-not $taskProcess.HasExited) { $taskProcess.Kill(); $taskProcess.WaitForExit() }
                $taskProcess.Dispose()
            }
        }
    }
} finally {
    $env:TEMP = $taskPreviousTemp
    $env:TMP = $taskPreviousTmp
    Remove-Item Env:WP1_AUTHORING_ONLY -ErrorAction SilentlyContinue
    foreach ($taskMode in $taskPreviousModes) { Set-Item "Env:$($taskMode.Name)" $taskMode.Value }
}
Write-Output "Logs: $taskLogDirectory"
