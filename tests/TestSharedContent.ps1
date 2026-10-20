param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$taskRepo=Split-Path $PSScriptRoot -Parent
$taskVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskInstallation=& $taskVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
$taskBuild=Join-Path $taskInstallation 'MSBuild/Current/Bin/MSBuild.exe'
$taskOutput=Join-Path $taskRepo ('generated/tests/shared-content-'+[Guid]::NewGuid().ToString('N'))
$taskProject=Join-Path $PSScriptRoot 'ReviewRegressionValidation.vcxproj'
& $taskBuild $taskProject '/t:CopySharedContent,CopyTestAppContent' "/p:Configuration=$Configuration" '/p:Platform=x64' '/p:SkipGameScripts=true' "/p:ValidationOutputRoot=$taskOutput\" '/nr:false' '/v:minimal' '/nologo'
if($LASTEXITCODE -ne 0) {throw 'Shared game content copy failed.'}
$taskContent=Join-Path $taskOutput "x64/$Configuration/ReviewRegressionValidation"
foreach($taskAsset in @('Assets/Project.json','Assets/Scenes/TitleStreet.json','Assets/Scenes/StreetJourney.json','Assets/Audio/Title/Select.wav','Assets/Scripts/StreetJourney.cpp','Shaders/Mesh.hlsl')) {
    $taskSource=Join-Path $taskRepo "Games/GuruttoWorld/$taskAsset"
    if (!(Test-Path -LiteralPath $taskSource) -and $taskAsset.StartsWith('Shaders/')) {
        $taskSource=Join-Path $taskRepo "Content/$taskAsset"
    }
    $taskCopied=Join-Path $taskContent $taskAsset
    if(!(Test-Path -LiteralPath $taskCopied) -or (Get-FileHash -LiteralPath $taskSource).Hash -ne (Get-FileHash -LiteralPath $taskCopied).Hash) {throw "Shared content missing or stale: $taskAsset"}
}
Write-Output "PASS: clean $Configuration test output contains current game scenes, audio, scripts and shaders"
