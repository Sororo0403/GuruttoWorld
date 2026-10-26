param(
    [Parameter(Mandatory = $true)][string]$Package,
    [ValidateSet('Development', 'Release')][string]$Configuration = 'Release',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskPackage = (Resolve-Path -LiteralPath $Package).Path
$taskFolder = Join-Path $taskRoot ('generated/tests/packaged-artifacts/' + [Guid]::NewGuid().ToString('N'))
$taskJapanese = [string][char]0x65e5 + [char]0x672c + [char]0x8a9e
$taskCopy = Join-Path $taskFolder ($taskJapanese + ' relocated package')
New-Item -ItemType Directory -Path $taskCopy -Force | Out-Null
Get-ChildItem -LiteralPath $taskPackage -Force | Copy-Item -Destination $taskCopy -Recurse -Force
if (!$SkipBuild) {
    $taskMsbuild = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe'
    & $taskMsbuild (Join-Path $PSScriptRoot 'PackagedArtifactValidation.vcxproj') /p:BuildProjectReferences=false /m:1 /nr:false /p:PreferredToolArchitecture=x64 /p:Platform=x64 "/p:Configuration=$Configuration" /nologo /v:minimal *> (Join-Path $taskFolder 'build.log')
    if ($LASTEXITCODE -ne 0) { throw "Packaged CPU test build failed: $taskFolder" }
}
$taskCpu = Join-Path $taskRoot "generated/tests/outputs/x64/$Configuration/PackagedArtifactValidation/PackagedArtifactValidation.exe"
$taskFiles = @(Get-ChildItem -LiteralPath $taskCopy -File -Recurse)
$taskExpected = @{}
foreach ($taskFile in $taskFiles) {
    $taskExpected[$taskFile.FullName] = (Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash
    $taskFile.IsReadOnly = $true
}
function Invoke-PackageProcess([string]$Executable, [string]$Arguments, [string]$Name) {
    $taskProcess = Start-Process -FilePath $Executable -ArgumentList $Arguments -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskFolder "$Name.log") -RedirectStandardError (Join-Path $taskFolder "$Name-error.log")
    $null = $taskProcess.Handle
    try {
        if (!$taskProcess.WaitForExit(600000)) { $taskProcess.Kill(); $taskProcess.WaitForExit(); throw "Package process timed out: $Name" }
        $taskProcess.WaitForExit()
        if ($taskProcess.ExitCode -ne 0) { throw "Package process failed: $Name; logs: $taskFolder" }
    } finally { $taskProcess.Dispose() }
}
function Invoke-PackageStartup([string]$Name, [bool]$ExpectCompile) {
    $taskPreviousLocal = $env:LOCALAPPDATA
    try {
        $env:LOCALAPPDATA = Join-Path $taskFolder "$Name-local"
        New-Item -ItemType Directory -Path $env:LOCALAPPDATA -Force | Out-Null
        Invoke-PackageProcess (Join-Path $taskCopy 'App.exe') '--validate-package' $Name
        $taskShaders = @(Get-ChildItem -LiteralPath (Join-Path $env:LOCALAPPDATA 'WP1/shader-cache') -Filter '*.artifact' -File -Recurse -ErrorAction SilentlyContinue)
        if (($ExpectCompile -and $taskShaders.Count -eq 0) -or (!$ExpectCompile -and $taskShaders.Count -ne 0)) {
            throw "Cold shader cache did not show the expected cooked hit/recompile: $Name"
        }
    } finally { $env:LOCALAPPDATA = $taskPreviousLocal }
}
Invoke-PackageProcess $taskCpu ('"' + $taskCopy + '" "' + (Join-Path $taskFolder 'hit-cache') + '" hit') 'cpu-hit'
Invoke-PackageStartup 'gpu-hit' $false
# 配布元は保持し、検証用コピーの読み取り専用成果物だけを意図的に破損させます。
$taskCpuArtifact = Get-ChildItem -LiteralPath (Join-Path $taskCopy 'Assets/Compiled/static-mesh') -Filter '*.artifact' -File | Select-Object -First 1
if (!$taskCpuArtifact) { throw 'Pass a package containing cooked static meshes.' }
$taskCorrupt = @($taskCpuArtifact) + @(Get-ChildItem -LiteralPath (Join-Path $taskCopy 'Engine/Shaders/Compiled') -Filter '*.artifact' -File -ErrorAction SilentlyContinue) + @(Get-ChildItem -LiteralPath (Join-Path $taskCopy 'Shaders/Compiled') -Filter '*.artifact' -File -ErrorAction SilentlyContinue)
foreach ($taskFile in $taskCorrupt) {
    $taskFile.IsReadOnly = $false
    $taskBytes = [IO.File]::ReadAllBytes($taskFile.FullName)
    $taskBytes[$taskBytes.Length - 1] = $taskBytes[$taskBytes.Length - 1] -bxor 1
    [IO.File]::WriteAllBytes($taskFile.FullName, $taskBytes)
    $taskFile.IsReadOnly = $true
    $taskExpected[$taskFile.FullName] = (Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash
}
Invoke-PackageProcess $taskCpu ('"' + $taskCopy + '" "' + (Join-Path $taskFolder 'recover-cache') + '" recover') 'cpu-recover'
Invoke-PackageStartup 'gpu-recover' $true
foreach ($taskFile in $taskFiles) {
    if ((Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash -cne $taskExpected[$taskFile.FullName]) {
        throw "Runtime modified a packaged file: $($taskFile.FullName)"
    }
}
$taskResult = [ordered]@{configuration=$Configuration;sourcePackage=$taskPackage;relocatedPackage=$taskCopy;cpuCookedHit='pass';cpuCorruptRecovery='pass';gpuCookedHit='pass';gpuCorruptRecovery='pass';packageFilesUnchanged=$taskFiles.Count}
$taskResult | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskFolder 'result.json') -Encoding utf8
Write-Output "PASS: relocated read-only package, cold CPU/shader caches and corrupt-artifact fallback; $taskFolder"
