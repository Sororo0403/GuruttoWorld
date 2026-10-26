$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $taskRoot 'scripts/EngineBuildInputs.ps1')
. (Join-Path $taskRoot 'scripts/FileHash.ps1')
$taskFixture = Join-Path $taskRoot ('generated/tests/engine-inputs/' + [Guid]::NewGuid().ToString('N'))

function Write-Input {
    param([string]$Relative, [string]$Value)
    $path = Join-Path $taskFixture $Relative
    New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force | Out-Null
    [IO.File]::WriteAllText($path, $Value, [Text.UTF8Encoding]::new($false))
}

Write-Input 'Engine/include/Engine/Api.h' 'public api'
Write-Input 'Engine/externals/Dependency.h' 'vendor api'
Write-Input 'scripts/BuildJolt.ps1' 'locked dependency revision'
Write-Input 'generated/dependencies/jolt-src/Jolt/Body.h' 'physics api'
Write-Input 'generated/outputs/Engine.lib' 'output is not an input source'
$taskInputs = @(Get-EngineBuildInputFiles -Root $taskFixture)
if ($taskInputs.Count -ne 3) { throw 'Canonical source inputs omit vendor headers or include generated output.' }
$taskNative = @(Get-EngineBuildInputFiles -Root $taskFixture -IncludeDependencies)
if ($taskNative.Count -ne 4) { throw 'SDK freshness inputs omit native dependency sources.' }
$taskVendor = Join-Path $taskFixture 'Engine/externals/Dependency.h'
$taskBefore = Get-Wp1FileHash -LiteralPath $taskVendor
Write-Input 'Engine/externals/Dependency.h' 'changed vendor api'
if ((Get-Wp1FileHash -LiteralPath $taskVendor) -ceq $taskBefore) { throw 'Vendor input change was not detected.' }
Write-Input 'SceneRuntime/include/SceneRuntime/NewApi.h' 'new api'
if (@(Get-EngineBuildInputFiles -Root $taskFixture).Count -ne 4) { throw 'New SDK source is absent from canonical inputs.' }
Write-Input 'SceneRuntime/SceneStorage.vcxproj' 'storage source ownership'
if (@(Get-EngineBuildInputFiles -Root $taskFixture).Count -ne 5) { throw 'Storage project is absent from canonical inputs.' }
Write-Input 'Tools/ProjectTool/ScriptBuild.cpp' 'native build orchestration'
Write-Input 'Tools/ProjectTool/ProjectTool.vcxproj' 'native tool project'
if (@(Get-EngineBuildInputFiles -Root $taskFixture).Count -ne 7) { throw 'Native project tool is absent from canonical inputs.' }
Write-Input 'Engine/EngineCore.vcxproj' 'CPU core source ownership'
if (@(Get-EngineBuildInputFiles -Root $taskFixture).Count -ne 8) { throw 'CPU core project is absent from canonical inputs.' }
Write-Input 'SceneRuntime/SceneRuntimeCore.vcxproj' 'runtime CPU source ownership'
if (@(Get-EngineBuildInputFiles -Root $taskFixture).Count -ne 9) { throw 'Runtime CPU project is absent from canonical inputs.' }
Write-Output 'PASS: canonical SDK inputs include vendor headers, dependency pins and optional native sources without output cycles'
