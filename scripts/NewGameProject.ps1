param(
    [Parameter(Mandatory=$true)][string]$Destination,
    [ValidatePattern('^[A-Za-z][A-Za-z0-9_]{0,63}$')][string]$Name='MyGame',
    [string]$TemplateContent
)
$ErrorActionPreference='Stop'
$engineRoot=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$destinationRoot=[IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $destinationRoot) { throw 'Choose a new folder; existing projects are never overwritten.' }
if (!$TemplateContent) { $TemplateContent=Join-Path $engineRoot 'Games/GuruttoWorld' }
$TemplateContent=[IO.Path]::GetFullPath($TemplateContent)
foreach ($required in @('Assets/Project.json','Shaders')) {
    if (!(Test-Path -LiteralPath (Join-Path $TemplateContent $required))) { throw "Template is missing $required" }
}
function Write-ProjectFile([string]$Path,[string]$Text) {
    [IO.File]::WriteAllText($Path,($Text.Replace("`r`n","`n").Replace("`n","`r`n")+"`r`n"),[Text.UTF8Encoding]::new($false))
}
New-Item -ItemType Directory -Path $destinationRoot | Out-Null
. (Join-Path $PSScriptRoot 'BuildContentSnapshot.ps1')
Copy-BuildContentSnapshot -Content $TemplateContent -Destination $destinationRoot
$guid=[Guid]::NewGuid().ToString().ToUpperInvariant()
$escapedRoot=[Security.SecurityElement]::Escape($engineRoot+'\')
$project=@'
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <GameProjectRoot>$(MSBuildThisFileDirectory)</GameProjectRoot>
    <GameContentRoot>$(MSBuildThisFileDirectory)</GameContentRoot>
    <GameName>__NAME__</GameName>
    <ProjectGuid>{__GUID__}</ProjectGuid>
    <WP1Root>__ROOT__</WP1Root>
  </PropertyGroup>
  <Import Project="$(WP1Root)App\App.vcxproj" />
</Project>
'@
Write-ProjectFile (Join-Path $destinationRoot "$Name.vcxproj") ($project.Replace('__NAME__',$Name).Replace('__GUID__',$guid).Replace('__ROOT__',$escapedRoot))
$solution=@'
<Solution>
  <Configurations>
    <BuildType Name="Debug" />
    <BuildType Name="Development" />
    <BuildType Name="Release" />
    <Platform Name="x64" />
  </Configurations>
  <Project Path="__NAME__.vcxproj" />
  <Project Path="__ROOT__Engine/Engine.vcxproj" />
  <Project Path="__ROOT__SceneRuntime/SceneRuntime.vcxproj" />
</Solution>
'@
Write-ProjectFile (Join-Path $destinationRoot "$Name.slnx") ($solution.Replace('__NAME__',$Name).Replace('__ROOT__',$escapedRoot.Replace('\','/')))
$descriptor=[ordered]@{version=1;name=$Name;engineRoot=$engineRoot;project="$Name.vcxproj";solution="$Name.slnx"}
Write-ProjectFile (Join-Path $destinationRoot 'WP1.project.json') ($descriptor | ConvertTo-Json)
Write-ProjectFile (Join-Path $destinationRoot '.gitignore') "generated/`nAssets/Scripts/Bin/`n*.vcxproj.user`n.vs/"
Write-Output "Created game project: $destinationRoot"
Write-Output "Build $Name.slnx (Debug / Development / Release, x64)."
