param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [string]$TestGameRoot = (Join-Path $PSScriptRoot '../../GuruttoWorld')
)
$ErrorActionPreference='Stop'
$taskRepo=Split-Path $PSScriptRoot -Parent
$TestGameRoot = [IO.Path]::GetFullPath($TestGameRoot)
$taskVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskInstallation=& $taskVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$taskBuild=Join-Path $taskInstallation 'MSBuild/Current/Bin/MSBuild.exe'
$taskOutput=Join-Path $taskRepo ('generated/tests/game-content-'+[Guid]::NewGuid().ToString('N'))
$taskProject=Join-Path $PSScriptRoot 'ReviewRegressionValidation.vcxproj'
& $taskBuild $taskProject '/t:CopyGameContent,CopyTestAppContent' "/p:Configuration=$Configuration" '/p:Platform=x64' '/p:SkipGameScripts=true' "/p:TestGameRoot=$TestGameRoot" "/p:ValidationOutputRoot=$taskOutput\" '/nr:false' '/v:minimal' '/nologo'
if($LASTEXITCODE -ne 0) {throw 'Game content copy failed.'}
$taskContent=Join-Path $taskOutput "x64/$Configuration/ReviewRegressionValidation"
foreach($taskAsset in @('Assets/Project.json','Assets/Scenes/TitleStreet.json','Assets/Scenes/StreetJourney.json','Assets/Audio/Title/Select.wav','Assets/Scripts/StreetJourney.cpp','Engine/Shaders/Mesh.hlsl')) {
    $taskSource=Join-Path $TestGameRoot $taskAsset
    if ($taskAsset.StartsWith('Engine/')) { $taskSource=Join-Path $taskRepo $taskAsset }
    $taskCopied=Join-Path $taskContent $taskAsset
    if(!(Test-Path -LiteralPath $taskCopied) -or (Get-FileHash -LiteralPath $taskSource).Hash -ne (Get-FileHash -LiteralPath $taskCopied).Hash) {throw "Game content missing or stale: $taskAsset"}
}
Write-Output "PASS: clean $Configuration test output contains current game scenes, audio, scripts and shaders"
