param(
    [Parameter(Mandatory = $true)][string]$BaselineFolder,
    [ValidateRange(1, 10)][int]$Runs = 3,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskBaseline = (Resolve-Path -LiteralPath $BaselineFolder).Path
$taskFolder = Join-Path $taskRoot ('generated/tests/runtime-comparison/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFolder -Force | Out-Null
$taskBuild = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe'
$taskNames = @('RuntimePerformanceValidation', 'SceneFramePerformanceValidation')
if (!$SkipBuild) {
    foreach ($taskName in $taskNames) {
        & $taskBuild (Join-Path $PSScriptRoot "$taskName.vcxproj") /m:1 /nr:false /p:PreferredToolArchitecture=x64 /p:Platform=x64 /p:Configuration=Release /nologo /v:minimal *> (Join-Path $taskFolder "$taskName-build.log")
        if ($LASTEXITCODE -ne 0) { throw "Performance build failed: $taskFolder" }
    }
}
$taskExecutions = [Collections.Generic.List[object]]::new()
Push-Location -LiteralPath $taskRoot
try {
    foreach ($taskRun in 1..$Runs) {
        # 旧版と新版の順序を交互にして、温度・実行順の偏りを抑えます。
        $taskVersions = if ($taskRun % 2) { @('baseline', 'current') } else { @('current', 'baseline') }
        foreach ($taskName in $taskNames) {
            foreach ($taskVersion in $taskVersions) {
                $taskExe = if ($taskVersion -eq 'baseline') { Join-Path $taskBaseline "$taskName.exe" } else { Join-Path $taskRoot "generated/tests/outputs/x64/Release/$taskName/$taskName.exe" }
                $taskStem = Join-Path $taskFolder "$taskName-$taskVersion-$taskRun"
                $taskProcess = Start-Process -FilePath $taskExe -ArgumentList ('"' + $taskStem + '.json"') -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput "$taskStem.log" -RedirectStandardError "$taskStem-error.log"
                $null = $taskProcess.Handle
                $taskPeak = 0L
                $taskClock = [Diagnostics.Stopwatch]::StartNew()
                try {
                    while (!$taskProcess.WaitForExit(100)) {
                        $taskProcess.Refresh()
                        $taskPeak = [Math]::Max($taskPeak, $taskProcess.PeakWorkingSet64)
                        if ($taskClock.Elapsed.TotalSeconds -ge 300) { $taskProcess.Kill(); $taskProcess.WaitForExit(); throw "Performance execution timed out: $taskStem" }
                    }
                    $taskProcess.WaitForExit()
                    if ($taskProcess.ExitCode -ne 0) { throw "Performance execution failed: $taskStem" }
                } finally { $taskProcess.Dispose() }
                $taskData = Get-Content -LiteralPath "$taskStem.json" -Raw | ConvertFrom-Json
                $taskExecutions.Add([pscustomobject]@{name=$taskName;version=$taskVersion;run=$taskRun;executable=$taskExe;executableSha256=(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash;peakWorkingSetBytes=$taskPeak;data=$taskData})
                Write-Output "PASS: $taskName $taskVersion run $taskRun"
            }
        }
    }
} finally { Pop-Location }
function Get-Median($Values) {
    $taskSorted = @($Values | Where-Object { $null -ne $_ } | Sort-Object)
    if ($taskSorted.Count -eq 0) { return $null }
    $taskMiddle = [int][Math]::Floor($taskSorted.Count / 2)
    if ($taskSorted.Count % 2) { return $taskSorted[$taskMiddle] }
    return ($taskSorted[$taskMiddle - 1] + $taskSorted[$taskMiddle]) / 2
}
$taskRows = [Collections.Generic.List[object]]::new()
foreach ($taskName in $taskNames) {
    $taskOld = @($taskExecutions | Where-Object { $_.name -eq $taskName -and $_.version -eq 'baseline' })
    $taskNew = @($taskExecutions | Where-Object { $_.name -eq $taskName -and $_.version -eq 'current' })
    $taskCount = $taskOld[0].data.scenarios.Count
    if ($taskNew[0].data.scenarios.Count -ne $taskCount) { throw 'Scenario counts differ.' }
    for ($taskIndex = 0; $taskIndex -lt $taskCount; ++$taskIndex) {
        $taskReference = $taskOld[0].data.scenarios[$taskIndex]
        foreach ($taskExecution in @($taskOld) + @($taskNew)) {
            $taskSample = $taskExecution.data.scenarios[$taskIndex]
            if ($taskSample.inputHash -cne $taskReference.inputHash -or $taskSample.resultHash -cne $taskReference.resultHash -or $taskSample.objects -ne $taskReference.objects -or $taskSample.measuredFrames -ne $taskReference.measuredFrames -or $taskSample.warmupFrames -ne $taskReference.warmupFrames) {
                throw "Input/result equivalence failed: $taskName scenario $taskIndex"
            }
        }
        $taskOldSamples = @($taskOld | ForEach-Object { $_.data.scenarios[$taskIndex] })
        $taskNewSamples = @($taskNew | ForEach-Object { $_.data.scenarios[$taskIndex] })
        # CPU単体と実フレームの計測が使う割当バイト数の項目名を統一します。
        $taskOldBytes = @($taskOldSamples | ForEach-Object { if ($null -ne $_.medianAllocatedBytes) { $_.medianAllocatedBytes } else { $_.medianRequestedBytes } })
        $taskNewBytes = @($taskNewSamples | ForEach-Object { if ($null -ne $_.medianAllocatedBytes) { $_.medianAllocatedBytes } else { $_.medianRequestedBytes } })
        $taskRows.Add([pscustomobject]@{name=$taskName;objects=$taskReference.objects;scripts=($taskReference.scripts -or $taskReference.scripted);physics=($taskReference.physics -eq $true);inputHash=$taskReference.inputHash;resultHash=$taskReference.resultHash;baselineMedianMs=(Get-Median $taskOldSamples.medianMs);currentMedianMs=(Get-Median $taskNewSamples.medianMs);baselineP95Ms=(Get-Median $taskOldSamples.p95Ms);currentP95Ms=(Get-Median $taskNewSamples.p95Ms);baselineP99Ms=(Get-Median $taskOldSamples.p99Ms);currentP99Ms=(Get-Median $taskNewSamples.p99Ms);baselineAllocations=(Get-Median $taskOldSamples.medianAllocations);currentAllocations=(Get-Median $taskNewSamples.medianAllocations);baselineAllocatedBytes=(Get-Median $taskOldBytes);currentAllocatedBytes=(Get-Median $taskNewBytes)})
    }
}
$taskDrivers = @(Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}' -ErrorAction SilentlyContinue | ForEach-Object { Get-ItemProperty -LiteralPath $_.PSPath -ErrorAction SilentlyContinue } | Where-Object DriverDesc | Select-Object DriverDesc, DriverVersion)
$taskSummary = [ordered]@{configuration='Release';runs=$Runs;recordedUtc=[DateTime]::UtcNow.ToString('o');cpu=[Microsoft.Win32.Registry]::GetValue('HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0','ProcessorNameString','');os=[Environment]::OSVersion.VersionString;displayDrivers=$taskDrivers;scope='CPU update transactions, no draw/Present; main-thread C++ allocations; medians of per-run median/p95/p99; sampled whole-process peak working sets';equivalentInputsAndResults=$true;rows=$taskRows;executions=$taskExecutions}
$taskSummary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $taskFolder 'comparison.json') -Encoding utf8
$taskRows | Format-Table name,objects,scripts,physics,baselineMedianMs,currentMedianMs -AutoSize
Write-Output "PASS: equivalent input/result hashes across $Runs alternating runs; $taskFolder"
