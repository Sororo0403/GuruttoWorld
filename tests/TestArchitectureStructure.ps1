param(
    [ValidateSet('Debug', 'Development', 'Release')][string[]]$Configurations = @('Debug', 'Development', 'Release'),
    [switch]$SkipGpu,
    [switch]$SkipHosts,
    [string]$GameProject = (Join-Path $PSScriptRoot '../../GuruttoWorld')
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'ProjectToolTestSupport.ps1')
$taskTool = Get-ProjectTool
$GameProject = [IO.Path]::GetFullPath($GameProject)
$taskLogs = Join-Path $taskRoot ('generated/architecture-fixes/validation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskLogs -Force | Out-Null
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskInstallation = & $taskVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (!$taskInstallation) { throw 'Visual Studio C++ is required.' }
$taskMsbuild = Join-Path $taskInstallation 'MSBuild/Current/Bin/MSBuild.exe'
$taskResults = [Collections.Generic.List[object]]::new()

function Build-ValidationProject {
    param([string]$Project, [string]$Configuration)
    $name = [IO.Path]::GetFileNameWithoutExtension($Project)
    $log = Join-Path $taskLogs "$name-$Configuration-build.log"
    $projectPath = if ([IO.Path]::IsPathRooted($Project)) { $Project } else { Join-Path $taskRoot $Project }
    & $taskMsbuild $projectPath "/p:Configuration=$Configuration" /p:Platform=x64 "/p:TestGameRoot=$GameProject" /p:PreferredToolArchitecture=x64 /m:1 /nr:false /v:minimal /nologo *> $log
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $log -Tail 30; throw "Build failed: $log" }
    Write-Output "PASS: build $name $Configuration"
}

function Invoke-ValidationProcess {
    param([string]$Name, [string]$Configuration, [string[]]$Arguments = @(), [string]$Executable)
    if (!$Executable) { $Executable = Join-Path $taskRoot "generated/tests/outputs/x64/$Configuration/$Name/$Name.exe" }
    $output = Join-Path $taskLogs "$Name-$Configuration.out"
    $errorLog = Join-Path $taskLogs "$Name-$Configuration.err"
    $parameters = @{
        FilePath = $Executable
        WorkingDirectory = $taskRoot
        WindowStyle = 'Hidden'
        PassThru = $true
        RedirectStandardOutput = $output
        RedirectStandardError = $errorLog
    }
    if ($Arguments.Count) { $parameters.ArgumentList = $Arguments }
    $process = Start-Process @parameters
    $null = $process.Handle
    try {
        if (!$process.WaitForExit(180000)) { throw "Validation timed out: $Name $Configuration" }
        if ($process.ExitCode -ne 0) {
            Get-Content -LiteralPath $output -Tail 12
            Get-Content -LiteralPath $errorLog -Tail 12
            throw "Validation failed: $Name $Configuration"
        }
        $taskResults.Add([ordered]@{ name = $Name; configuration = $Configuration; output = $output })
        $taskResults | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskLogs 'results.json') -Encoding UTF8
        Write-Output "PASS: run $Name $Configuration"
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

Push-Location $taskRoot
try {
    Write-Output "Validation logs: $taskLogs"
    & (Join-Path $PSScriptRoot 'TestScriptPublication.ps1')
    & (Join-Path $PSScriptRoot 'TestEngineBuildInputs.ps1')
    & (Join-Path $PSScriptRoot 'TestSceneLayers.ps1')
    foreach ($configuration in $Configurations) {
        foreach ($project in @('SceneDataValidation', 'DocumentSessionValidation', 'ArchitectureStructureValidation', 'ArchitectureContractValidation', 'ArchitectureAuditFixValidation')) {
            if ($SkipGpu -and $project -eq 'ArchitectureAuditFixValidation') { continue }
            Build-ValidationProject -Project "tests/$project.vcxproj" -Configuration $configuration
            $arguments = @()
            if ($project -eq 'ArchitectureStructureValidation' -and !$SkipGpu) { $arguments = @('--gpu') }
            Invoke-ValidationProcess -Name $project -Configuration $configuration -Arguments $arguments
        }
        if (!$SkipHosts) {
            if ($configuration -ne 'Release') {
                Build-ValidationProject -Project 'Editor/Editor.vcxproj' -Configuration $configuration
                Invoke-ValidationProcess -Name 'Editor-project' -Configuration $configuration `
                    -Executable (Join-Path $taskRoot "generated/outputs/x64/$configuration/Editor/Editor.exe") `
                    -Arguments @('--validate-project', '--project', ('"' + $GameProject + '"'))
            }
            Build-ValidationProject -Project (Join-Path $GameProject 'GuruttoWorld.vcxproj') -Configuration $configuration
            if (!$SkipGpu) {
                Invoke-ValidationProcess -Name 'GuruttoWorld-package' -Configuration $configuration `
                    -Executable (Join-Path $GameProject "generated/outputs/x64/$configuration/App/GuruttoWorld.exe") `
                    -Arguments @('--validate-package')
            }
        }
        $nativeLog = Join-Path $taskLogs "native-$configuration.log"
        & (Join-Path $PSScriptRoot 'TestNativeBuildIdentity.ps1') -Configuration $configuration *> $nativeLog
        if (!$?) { Get-Content -LiteralPath $nativeLog -Tail 20; throw "Native SDK validation failed: $nativeLog" }
        & (Join-Path $PSScriptRoot 'TestScriptSchemaLoading.ps1') -Configuration $configuration *> (Join-Path $taskLogs "schema-$configuration.log")
        if (!$?) { throw "Direct JSON schema loading validation failed: $configuration" }
        Write-Output "PASS: native SDK and direct JSON schema loading $configuration"
        if ($configuration -eq 'Development' -and !$SkipGpu) {
            & $taskTool build-scripts -Engine $taskRoot -Configuration Development -Content $GameProject *> (Join-Path $taskLogs 'playback-scripts.log')
            if ($LASTEXITCODE -ne 0) { throw 'Playback script module build failed.' }
            Build-ValidationProject -Project 'tests/GamePlaybackFocusedValidation.vcxproj' -Configuration Development
            Invoke-ValidationProcess -Name 'GamePlaybackFocusedValidation' -Configuration Development
        }
    }
} finally {
    Pop-Location
}
