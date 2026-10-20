param(
    [ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Debug',
    [switch]$Baseline,
    [string[]]$Require = @(),
    [string]$Module,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskResults = Join-Path $taskRoot 'generated/design-migration'
New-Item -ItemType Directory -Path $taskResults -Force | Out-Null
Push-Location $taskRoot
try {
    if (!$SkipBuild) {
        $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $taskInstallation = & $taskVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
        if (!$taskInstallation) { throw 'Visual Studio C++ が必要です。' }
        $taskBuildLog = Join-Path $taskResults "contracts-$Configuration-build.log"
        & (Join-Path $taskInstallation 'MSBuild/Current/Bin/MSBuild.exe') tests/ArchitectureContractValidation.vcxproj "/p:Configuration=$Configuration" /p:Platform=x64 /p:PreferredToolArchitecture=x64 /m:1 /nr:false /v:minimal /nologo *> $taskBuildLog
        if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $taskBuildLog -Tail 25; throw "ビルドに失敗しました: $taskBuildLog" }
    }
    $taskExecutable = Join-Path $taskRoot "generated/tests/outputs/x64/$Configuration/ArchitectureContractValidation/ArchitectureContractValidation.exe"
    $taskArguments = @()
    if ($Baseline) { $taskArguments += '--baseline' }
    if ($Module) { $taskArguments += '--module=' + (Resolve-Path -LiteralPath $Module).Path }
    foreach ($taskContract in $Require) {
        if ($taskContract -notmatch '^[a-z][a-z0-9-]+$') { throw '契約 ID が不正です。' }
        $taskArguments += "--require=$taskContract"
    }
    & $taskExecutable @taskArguments | Tee-Object -FilePath (Join-Path $taskResults "contracts-$Configuration-results.log")
    if ($LASTEXITCODE -ne 0) { throw '設計契約の必須検証に失敗しました。' }
} finally {
    Pop-Location
}
