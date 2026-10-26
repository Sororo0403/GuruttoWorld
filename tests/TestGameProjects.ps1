param(
    [switch]$Build,
    [ValidateSet('Debug', 'Development', 'Release')][string[]]$Configurations = @('Debug', 'Development', 'Release')
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'ProjectToolTestSupport.ps1')
$testRoot=Join-Path $repoRoot ('generated/tests/game-projects-'+[Guid]::NewGuid().ToString('N'))
$gameRoot=Join-Path $testRoot 'External Game'
New-Item -ItemType Directory -Path $testRoot | Out-Null
$sourceSettings=Join-Path $repoRoot 'Tools/GameProject/Templates/MinimalGame/Assets/Project.json'
$sourceText=[IO.File]::ReadAllText($sourceSettings)
& (Get-ProjectTool) new-project -Engine $repoRoot -Destination $gameRoot -Name SeparateGame
if ($LASTEXITCODE -ne 0) { throw 'Project creation failed.' }
$descriptor=Get-Content -LiteralPath (Join-Path $gameRoot 'WP1.project.json') -Raw | ConvertFrom-Json
if ($descriptor.version -ne 1 -or $descriptor.engineRoot -ne $repoRoot -or $descriptor.solution -ne 'SeparateGame.slnx') { throw 'Incorrect descriptor.' }
[xml]$solution=Get-Content -LiteralPath (Join-Path $gameRoot $descriptor.solution) -Raw
if (@($solution.Solution.Project | Where-Object { $_.Path -match 'Editor' }).Count) { throw 'Game solution includes Editor.' }
foreach ($projectEntry in $solution.Solution.Project) {
    $reference=$projectEntry.Path
    if (![IO.Path]::IsPathRooted($reference)) { $reference=Join-Path $gameRoot $reference }
    if (!(Test-Path -LiteralPath $reference)) { throw "Missing solution project: $($projectEntry.Path)" }
}
$settingsPath=Join-Path $gameRoot 'Assets/Project.json'
$settings=Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
$settings.title='Independent game project validation'
$settings | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $settingsPath -Encoding UTF8
$before=[IO.File]::ReadAllText($settingsPath)
try {
    $ErrorActionPreference='Continue'
    & (Get-ProjectTool) new-project -Engine $repoRoot -Destination $gameRoot -Name SeparateGame *> (Join-Path $testRoot 'existing-folder.log')
    $existingExitCode=$LASTEXITCODE
} finally { $ErrorActionPreference='Stop' }
if ($existingExitCode -eq 0 -or [IO.File]::ReadAllText($settingsPath) -cne $before) { throw 'Existing project was overwritten.' }
$invalidRoot=Join-Path $testRoot 'InvalidTemplate'
try {
    $ErrorActionPreference='Continue'
    & (Get-ProjectTool) new-project -Engine $repoRoot -Destination $invalidRoot -Name SeparateGame -TemplateContent $testRoot *> (Join-Path $testRoot 'invalid-template.log')
    $invalidExitCode=$LASTEXITCODE
} finally { $ErrorActionPreference='Stop' }
if ($invalidExitCode -eq 0 -or (Test-Path -LiteralPath $invalidRoot)) { throw 'Invalid template was accepted.' }
if ($Build) {
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation=& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
    if (!$installation) { throw 'Visual Studio is required.' }
    $msbuild=Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
    Set-Content -LiteralPath (Join-Path $gameRoot 'Assets/IndependentMarker.txt') -Value 'Separate game assets'
    foreach ($configuration in $Configurations) {
        & $msbuild (Join-Path $gameRoot $descriptor.solution) "/p:Configuration=$configuration" '/p:Platform=x64' '/p:PreferredToolArchitecture=x64' '/m:1' '/nr:false' '/v:minimal' '/nologo' *> (Join-Path $testRoot "$configuration.log")
        if ($LASTEXITCODE -ne 0) { throw "Game build failed: $configuration; see $testRoot" }
        $output=Join-Path $gameRoot "generated/outputs/x64/$configuration/App"
        foreach ($required in @('SeparateGame.exe','Assets/IndependentMarker.txt',"Assets/Scripts/Bin/$configuration/module.json")) {
            if (!(Test-Path -LiteralPath (Join-Path $output $required))) { throw "Game output is missing: $required" }
        }
        $builtSettings=Get-Content -LiteralPath (Join-Path $output 'Assets/Project.json') -Raw | ConvertFrom-Json
        if ($builtSettings.title -ne $settings.title) { throw 'Build used engine sample content instead of game content.' }
        $validation=Start-Process -FilePath (Join-Path $output 'SeparateGame.exe') -ArgumentList '--validate-package' -WorkingDirectory $output -WindowStyle Hidden -PassThru
        if (!$validation.WaitForExit(180000)) { $validation.Kill(); throw 'Game validation timed out.' }
        if ($validation.ExitCode -ne 0) { throw "Game validation failed: $configuration" }
    }
}
if ([IO.File]::ReadAllText($sourceSettings) -cne $sourceText) { throw 'Engine sample settings were changed.' }
$editor=Join-Path $repoRoot 'generated/outputs/x64/Debug/Editor/Editor.exe'
if ($Build) {
    if (!(Test-Path -LiteralPath $editor)) { throw 'Build the Debug editor before running editor project validation.' }
    $arguments='--validate-project --project "'+$gameRoot+'"'
    $validation=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (!$validation.WaitForExit(30000)) { $validation.Kill(); throw 'Editor project validation timed out.' }
    if ($validation.ExitCode -ne 0) { throw 'Editor could not open the independent game project.' }
    $descriptorPath=Join-Path $gameRoot 'WP1.project.json'
    $descriptorText=[IO.File]::ReadAllText($descriptorPath)
    try {
        $descriptor.version=999
        $descriptor | ConvertTo-Json | Set-Content -LiteralPath $descriptorPath -Encoding UTF8
        $validation=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if (!$validation.WaitForExit(30000)) { $validation.Kill(); throw 'Invalid editor project validation timed out.' }
        if ($validation.ExitCode -eq 0) { throw 'Editor accepted an unsupported project version.' }
    } finally { [IO.File]::WriteAllText($descriptorPath,$descriptorText,[Text.UTF8Encoding]::new($false)) }
}
Write-Output "PASS: independent game project creation, references, overwrite protection and source isolation; results: $testRoot"
