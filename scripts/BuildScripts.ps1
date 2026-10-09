param(
    [ValidateSet('Debug','Development','Release')][string]$Configuration='Development',
    [string]$Content,
    [string]$OutputContent,
    [string]$RunId=([Guid]::NewGuid().ToString('N'))
)

$ErrorActionPreference='Stop'
if ($RunId -notmatch '^[a-zA-Z0-9_-]{1,64}$') { throw 'Invalid script build run ID.' }
$repoRoot=Split-Path $PSScriptRoot -Parent
if (!$Content) { $Content=Join-Path $repoRoot 'Content' }
if (!$OutputContent) { $OutputContent=$Content }
$Content=[IO.Path]::GetFullPath($Content)
$OutputContent=[IO.Path]::GetFullPath($OutputContent)
$buildRoot=Join-Path $repoRoot 'generated/script-builds'
$workspace=Join-Path $buildRoot $RunId
New-Item -ItemType Directory -Path $workspace -Force | Out-Null
$resultPath=Join-Path $buildRoot "$RunId.json"
$logPath=Join-Path $buildRoot "$RunId.log"
Start-Transcript -Path $logPath -Force | Out-Null
function Get-SourceHashes {
    param([object[]]$Files)
    $hashes=[ordered]@{}
    foreach ($sourceFile in $Files) { $hashes[$sourceFile.FullName]=(Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash }
    return $hashes
}
function Escape-Xml([string]$Value) { return [Security.SecurityElement]::Escape($Value) }
function Get-TrackedFiles([string]$Folder) {
    if (!(Test-Path -LiteralPath $Folder)) { return @() }
    return @(Get-ChildItem -LiteralPath $Folder -File -Recurse | Where-Object { $_.Extension -in @('.cpp','.h','.hpp') } | Sort-Object FullName)
}
function Set-ScriptProgress([int]$Step,[string]$Phase) {
    $progressPath=Join-Path $buildRoot "$RunId.progress.json"
    [ordered]@{step=$Step;phase=$Phase} | ConvertTo-Json | Set-Content -LiteralPath "$progressPath.tmp" -Encoding UTF8
    Move-Item -LiteralPath "$progressPath.tmp" -Destination $progressPath -Force
}
try {
    Set-ScriptProgress 0 'ゲーム処理ソースを確認中'
    $sourceRoot=Join-Path $Content 'Assets/Scripts'
    $tracked=@(Get-TrackedFiles $sourceRoot)
    $sources=@($tracked | Where-Object { $_.Extension -eq '.cpp' })
    if ($sources.Count -gt 256) { throw 'At most 256 script source files are supported.' }
    if (@($sources | Group-Object BaseName | Where-Object { $_.Count -gt 1 }).Count) { throw 'Script source base names must be unique across folders.' }
    foreach ($sourceFile in $sources) {
        if ($sourceFile.BaseName -notmatch '^[a-zA-Z_][a-zA-Z0-9_]{0,63}$') { throw "Script filename must be a C++ identifier: $($sourceFile.Name)" }
    }
    $before=Get-SourceHashes $tracked
    $outputFolder=Join-Path $OutputContent "Assets/Scripts/Bin/$Configuration"
    New-Item -ItemType Directory -Path $outputFolder -Force | Out-Null
    $manifestPath=Join-Path $outputFolder 'module.json'
    if (!$sources.Count) {
        [ordered]@{version=1;file='';configuration=$Configuration;sources=@{}} | ConvertTo-Json | Set-Content -LiteralPath "$manifestPath.tmp" -Encoding UTF8
        Move-Item -LiteralPath "$manifestPath.tmp" -Destination $manifestPath -Force
    } else {
        $engineLibrary=Join-Path $repoRoot "generated/outputs/x64/$Configuration/Engine/Engine.lib"
        $sceneLibrary=Join-Path $repoRoot "generated/outputs/x64/$Configuration/SceneRuntime/SceneRuntime.lib"
        if (!(Test-Path -LiteralPath $engineLibrary) -or !(Test-Path -LiteralPath $sceneLibrary)) { throw 'Build the matching Engine and SceneRuntime configuration first.' }
        $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $installation=& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
        if (!$installation) { throw 'Visual Studio with C++ and MSBuild is required.' }
        $msbuild=Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
        $aggregate='#include <SceneRuntime/ScriptModuleApi.h>'+"`r`n"
        foreach ($sourceFile in $sources) { $aggregate+="bool Register_$($sourceFile.BaseName)(void*,SceneRuntime::RegisterModuleScript);`r`n" }
        $aggregate+='extern "C" __declspec(dllexport) bool Wp1RegisterScripts(const SceneRuntime::ScriptModuleAbi* abi,void* context,SceneRuntime::RegisterModuleScript add)'+"`r`n{`r`n"
        $aggregate+='    if (!abi || !add || *abi!=SceneRuntime::ScriptModuleAbi{}) return false;'+"`r`n"
        foreach ($sourceFile in $sources) { $aggregate+="    if (!Register_$($sourceFile.BaseName)(context,add)) return false;`r`n" }
        $aggregate+="    return true;`r`n}`r`n"
        $aggregatePath=Join-Path $workspace 'ScriptModule.cpp'
        [IO.File]::WriteAllText($aggregatePath,$aggregate,[Text.UTF8Encoding]::new($false))
        $compileItems='<ClCompile Include="'+(Escape-Xml $aggregatePath)+'" />'
        foreach ($sourceFile in $sources) { $compileItems+='<ClCompile Include="'+(Escape-Xml $sourceFile.FullName)+'" />' }
        $definitions='NDEBUG;_WINDOWS'
        $runtimeLibrary='MultiThreadedDLL'
        $useDebug='false'
        if ($Configuration -eq 'Debug') { $definitions='_DEBUG;_WINDOWS'; $runtimeLibrary='MultiThreadedDebugDLL'; $useDebug='true' }
        if ($Configuration -eq 'Development') { $definitions+=';ENGINE_DEVELOPMENT' }
        $includePaths=(Escape-Xml (Join-Path $repoRoot 'SceneRuntime/include'))+';'+(Escape-Xml (Join-Path $repoRoot 'Engine/include'))
        $includePaths+=';'+(Escape-Xml (Join-Path $repoRoot 'Engine/externals/imgui'))
        $libraries=(Escape-Xml $sceneLibrary)+';'+(Escape-Xml $engineLibrary)
        $nativeOutput=Escape-Xml ($workspace+'/bin/')
        $nativeIntermediate=Escape-Xml ($workspace+'/obj/')
        $project=@"
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations"><ProjectConfiguration Include="$Configuration|x64"><Configuration>$Configuration</Configuration><Platform>x64</Platform></ProjectConfiguration></ItemGroup>
  <PropertyGroup Label="Globals"><VCProjectVersion>18.0</VCProjectVersion><WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion></PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration"><ConfigurationType>DynamicLibrary</ConfigurationType><PlatformToolset>v145</PlatformToolset><UseDebugLibraries>$useDebug</UseDebugLibraries></PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup><OutDir>$nativeOutput</OutDir><IntDir>$nativeIntermediate</IntDir><TargetName>GameScripts-$RunId</TargetName><LinkIncremental>false</LinkIncremental></PropertyGroup>
  <ItemDefinitionGroup><ClCompile><LanguageStandard>stdcpp20</LanguageStandard><ExceptionHandling>Sync</ExceptionHandling><DebugInformationFormat>ProgramDatabase</DebugInformationFormat><ConformanceMode>true</ConformanceMode><RuntimeLibrary>$runtimeLibrary</RuntimeLibrary><PreprocessorDefinitions>$definitions;%(PreprocessorDefinitions)</PreprocessorDefinitions><AdditionalIncludeDirectories>$includePaths;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories><AdditionalOptions>/utf-8 /bigobj %(AdditionalOptions)</AdditionalOptions><WarningLevel>Level4</WarningLevel><TreatWarningAsError>true</TreatWarningAsError></ClCompile><Link><AdditionalDependencies>$libraries;%(AdditionalDependencies)</AdditionalDependencies><GenerateDebugInformation>true</GenerateDebugInformation><TreatLinkerWarningAsErrors>true</TreatLinkerWarningAsErrors></Link></ItemDefinitionGroup>
  <ItemGroup>$compileItems</ItemGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
"@
        $projectPath=Join-Path $workspace 'GameScripts.vcxproj'
        [IO.File]::WriteAllText($projectPath,$project,[Text.UTF8Encoding]::new($false))
        Set-ScriptProgress 1 'C++ゲーム処理をコンパイル中'
        & $msbuild $projectPath "/p:Configuration=$Configuration" '/p:Platform=x64' '/m' '/v:minimal' '/nologo'
        if ($LASTEXITCODE -ne 0) { throw "Script compilation failed ($LASTEXITCODE); see $logPath" }
        $current=@(Get-TrackedFiles $sourceRoot)
        $after=Get-SourceHashes $current
        if (($before | ConvertTo-Json -Compress) -ne ($after | ConvertTo-Json -Compress)) { throw 'Script sources changed during compilation; the previous module was retained.' }
        $filename="GameScripts-$RunId.dll"
        Set-ScriptProgress 3 'コンパイル済み処理を保存中'
        Copy-Item -LiteralPath (Join-Path $workspace "bin/$filename") -Destination (Join-Path $outputFolder $filename)
        $symbols=Join-Path $workspace "bin/GameScripts-$RunId.pdb"
        if (Test-Path -LiteralPath $symbols) { Copy-Item -LiteralPath $symbols -Destination $outputFolder }
        $publicHashes=[ordered]@{}
        foreach ($sourceFile in $tracked) { $publicHashes[$sourceFile.FullName.Substring($sourceRoot.Length).TrimStart('\','/').Replace('\','/')]=$before[$sourceFile.FullName] }
        [ordered]@{version=1;file=$filename;configuration=$Configuration;sources=$publicHashes} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$manifestPath.tmp" -Encoding UTF8
        Move-Item -LiteralPath "$manifestPath.tmp" -Destination $manifestPath -Force
    }
    [ordered]@{success=$true;configuration=$Configuration;manifest=$manifestPath;log=$logPath} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
    Set-ScriptProgress 4 'コンパイル完了'
    Write-Output 'Script module build complete.'
} catch {
    [ordered]@{success=$false;error=$_.Exception.Message;log=$logPath} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
    Write-Output $_.Exception.Message
    exit 1
} finally { Stop-Transcript | Out-Null }
