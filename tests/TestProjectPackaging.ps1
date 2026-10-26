param(
    [ValidateSet('Development', 'Release')][string]$Configuration = 'Development',
    [string]$Project,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ProjectToolTestSupport.ps1')
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskTool = Get-ProjectTool
if (!$Project) {
    if ($SkipBuild) { throw 'Pass an already built -Project when using -SkipBuild.' }
    $taskJapanese = [string][char]0x65e5 + [char]0x672c + [char]0x8a9e
    $Project = Join-Path $taskRoot ('generated/tests/package ' + $taskJapanese + '/' + [Guid]::NewGuid().ToString('N'))
    & $taskTool new-project --engine $taskRoot --destination $Project --name PackageValidation
    if ($LASTEXITCODE -ne 0) { throw 'Package test project creation failed.' }
}
$Project = [IO.Path]::GetFullPath($Project)
$taskRunId = [Guid]::NewGuid().ToString('N')
$taskArguments = @('build-player', '--engine', $taskRoot, '--project', $Project,
    '--configuration', $Configuration, '--run-id', $taskRunId)
if ($SkipBuild) { $taskArguments += '--skip-build' }
& $taskTool @taskArguments
if ($LASTEXITCODE -ne 0) { throw 'Native package build failed; see the project generated/builds logs.' }
$taskResultPath = Join-Path $Project "generated/builds/$taskRunId.json"
$taskResult = Get-Content -LiteralPath $taskResultPath -Raw | ConvertFrom-Json
if (!$taskResult.success) { throw 'Native package build did not publish success.' }
$taskPackage = $taskResult.package
$taskPackageManifest = Join-Path $taskPackage 'package.json'
$taskManifest = Get-Content -LiteralPath $taskPackageManifest -Raw | ConvertFrom-Json
if ($taskManifest.configuration -ne $Configuration -or !$taskManifest.createdUtc -or
    $taskManifest.projectId -cnotmatch '^[a-f0-9]{32}$' -or
    $taskManifest.engineBuild -cnotmatch '^[a-f0-9]{16}$' -or $taskManifest.scriptAbi -ne 18 -or
    $taskManifest.settings.startupScene -ne $taskManifest.startupScene -or
    $taskManifest.scriptModule.configuration -ne $Configuration -or
    $taskManifest.executableRevision -ine (Get-FileHash -LiteralPath (Join-Path $taskPackage 'App.exe') -Algorithm SHA256).Hash) {
    throw 'Incorrect package provenance metadata.'
}
if (!(Test-Path -LiteralPath (Join-Path $taskPackage $taskManifest.startupScene) -PathType Leaf)) {
    throw 'Resolved package startup scene is missing.'
}
foreach ($taskRequired in @('App.exe', 'WP1.runtime.json', 'Engine/Shaders/Mesh.hlsl',
        "Assets/Scripts/Bin/$Configuration/module.json", 'vcruntime140.dll', 'Licenses')) {
    if (!(Test-Path -LiteralPath (Join-Path $taskPackage $taskRequired))) { throw "Missing package file: $taskRequired" }
}
foreach ($taskEntry in $taskManifest.files) {
    if ($taskEntry.path.Contains('\') -or [IO.Path]::IsPathRooted($taskEntry.path) -or
        $taskEntry.path.Split('/') -contains '..') { throw 'Package manifest contains a non-portable path.' }
    $taskFile = Get-Item -LiteralPath (Join-Path $taskPackage $taskEntry.path)
    if ($taskFile.Length -ne $taskEntry.bytes -or
        (Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash -ine $taskEntry.sha256) {
        throw "Packaged content differs from the manifest: $($taskEntry.path)"
    }
}
$taskManifestBefore = [IO.File]::ReadAllText($taskPackageManifest)
$taskExecutableBefore = (Get-FileHash -LiteralPath (Join-Path $taskPackage 'App.exe') -Algorithm SHA256).Hash
# 同じ実行IDを再送しても、公開済み配布物を上書きしないことを確認します。
$taskDuplicateLog = Join-Path $Project "generated/builds/$taskRunId-duplicate.log"
try {
    $ErrorActionPreference = 'Continue'
    & $taskTool @taskArguments *> $taskDuplicateLog
    $taskDuplicateExit = $LASTEXITCODE
} finally { $ErrorActionPreference = 'Stop' }
if ($taskDuplicateExit -eq 0 -or [IO.File]::ReadAllText($taskPackageManifest) -cne $taskManifestBefore -or
    (Get-FileHash -LiteralPath (Join-Path $taskPackage 'App.exe') -Algorithm SHA256).Hash -cne $taskExecutableBefore) {
    throw 'A repeated package run overwrote the published package.'
}
if (Test-Path -LiteralPath ($taskPackage + '.building')) { throw 'A successful package left its staging folder behind.' }
Write-Output "PASS: native $Configuration packaging, shader/GPU startup validation, manifest integrity and overwrite protection; $taskPackage"
