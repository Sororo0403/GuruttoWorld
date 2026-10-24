param([ValidateRange(1, 20)][int]$Runs = 5, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskOutput = Join-Path $taskRoot ('generated/architecture-fixes/performance-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskInstallation = & $taskVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (!$taskInstallation) { throw 'Visual Studio C++ is required.' }
$taskMsbuild = Join-Path $taskInstallation 'MSBuild/Current/Bin/MSBuild.exe'
Push-Location $taskRoot
try {
    foreach ($name in @('SceneExecutionProfile', 'PrefabResolutionProfile')) {
        if (!$SkipBuild) {
            & $taskMsbuild "tests/$name.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64 /m:1 /nr:false /v:minimal /nologo *> (Join-Path $taskOutput "$name-build.log")
            if ($LASTEXITCODE -ne 0) { throw "Profile build failed: $name; logs: $taskOutput" }
        }
        for ($run = 1; $run -le $Runs; ++$run) {
            $process = Start-Process -FilePath (Join-Path $taskRoot "generated/tests/outputs/x64/Release/$name/$name.exe") `
                -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput (Join-Path $taskOutput "$name-$run.csv") `
                -RedirectStandardError (Join-Path $taskOutput "$name-$run.err")
            $null = $process.Handle
            try {
                if (!$process.WaitForExit(180000)) { throw "Profile timed out: $name" }
                if ($process.ExitCode -ne 0) { throw "Profile failed: $name; logs: $taskOutput" }
            } finally {
                if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
                $process.Dispose()
            }
        }
        Write-Output "PASS: $name, $Runs runs"
    }
    $taskFrames = @(Get-ChildItem -LiteralPath $taskOutput -Filter 'SceneExecutionProfile-*.csv' | ForEach-Object { Import-Csv -LiteralPath $_.FullName })
    $taskSummary = foreach ($group in ($taskFrames | Group-Object objects, physics, workload)) {
        $times = @($group.Group | ForEach-Object { [double]::Parse($_.frame_ms, [Globalization.CultureInfo]::InvariantCulture) } | Sort-Object)
        $middle = [int][Math]::Floor($times.Count / 2)
        $median = $times[$middle]
        if ($times.Count % 2 -eq 0) { $median = ($times[$middle - 1] + $times[$middle]) / 2 }
        [pscustomobject]@{ objects = $group.Group[0].objects; physics = $group.Group[0].physics;
            workload = $group.Group[0].workload; runs = $times.Count;
            median_ms = $median; min_ms = $times[0]; max_ms = $times[-1] }
    }
    $taskSummary | Export-Csv -LiteralPath (Join-Path $taskOutput 'summary.csv') -NoTypeInformation -Encoding UTF8
    $taskSummary | Format-Table -AutoSize
    $taskPrefabs = @(Get-ChildItem -LiteralPath $taskOutput -Filter 'PrefabResolutionProfile-*.csv' | ForEach-Object { Import-Csv -LiteralPath $_.FullName })
    $taskPrefabSummary = foreach ($group in ($taskPrefabs | Group-Object instances)) {
        $times = @($group.Group | ForEach-Object { [double]::Parse($_.refresh_ms, [Globalization.CultureInfo]::InvariantCulture) } | Sort-Object)
        $middle = [int][Math]::Floor($times.Count / 2)
        $median = $times[$middle]
        if ($times.Count % 2 -eq 0) { $median = ($times[$middle - 1] + $times[$middle]) / 2 }
        [pscustomobject]@{ instances = $group.Group[0].instances; objects = $group.Group[0].objects;
            dependency_reads = $group.Group[0].dependency_reads_per_refresh; runs = $times.Count;
            median_ms = $median; min_ms = $times[0]; max_ms = $times[-1] }
    }
    $taskPrefabSummary | Export-Csv -LiteralPath (Join-Path $taskOutput 'summary-prefab.csv') -NoTypeInformation -Encoding UTF8
    $taskPrefabSummary | Format-Table -AutoSize
    Write-Output "Performance logs: $taskOutput"
} finally {
    Pop-Location
}
