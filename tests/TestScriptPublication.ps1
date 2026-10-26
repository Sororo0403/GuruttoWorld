param([ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Debug')

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskValidation = Join-Path $taskRoot "generated/tests/outputs/x64/$Configuration/ProjectToolValidation/ProjectToolValidation.exe"
if (!(Test-Path -LiteralPath $taskValidation)) {
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $taskInstallation = & $taskVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
    if (!$taskInstallation) { throw 'Visual Studio with MSBuild is required.' }
    $taskMsbuild = Join-Path $taskInstallation 'MSBuild/Current/Bin/MSBuild.exe'
    & $taskMsbuild (Join-Path $PSScriptRoot 'ProjectToolValidation.vcxproj') "/p:Configuration=$Configuration" /p:Platform=x64 /p:PreferredToolArchitecture=x64 /m:1 /nr:false /v:minimal /nologo | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'C++ project tool validation build failed.' }
}
& $taskValidation $taskRoot
if ($LASTEXITCODE -ne 0) { throw 'C++ project tool validation failed.' }
