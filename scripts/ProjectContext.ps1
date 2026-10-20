function Get-ProjectEngineRoot {
    param([string]$Content, [object]$Descriptor)
    if (!$Descriptor) {
        $Descriptor = Get-Content -LiteralPath (Join-Path $Content 'WP1.project.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    if ($Descriptor.engineRoot -isnot [string] -or [string]::IsNullOrWhiteSpace($Descriptor.engineRoot) -or $Descriptor.engineRoot.Contains([char]0)) {
        throw 'Game project has no valid engine reference.'
    }
    $projectEngine = $Descriptor.engineRoot
    if (![IO.Path]::IsPathRooted($projectEngine)) { $projectEngine = Join-Path $Content $projectEngine }
    return [IO.Path]::GetFullPath($projectEngine).TrimEnd('\', '/')
}

function Get-ProjectContext {
    param([string]$Project, [string]$RequireEngineRoot)
    $projectRoot = [IO.Path]::GetFullPath($Project)
    if ([IO.Path]::GetFileName($projectRoot) -eq 'WP1.project.json') { $projectRoot = Split-Path $projectRoot -Parent }
    $projectDescriptor = Get-Content -LiteralPath (Join-Path $projectRoot 'WP1.project.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (($projectDescriptor.version -isnot [int] -and $projectDescriptor.version -isnot [long]) -or $projectDescriptor.version -ne 1) {
        throw 'Unsupported game project version.'
    }
    if ($projectDescriptor.PSObject.Properties.Name -contains 'engineContract') {
        if (($projectDescriptor.engineContract -isnot [int] -and $projectDescriptor.engineContract -isnot [long]) -or $projectDescriptor.engineContract -ne 1) {
            throw 'Unsupported engine contract version.'
        }
    }
    $projectName = $projectDescriptor.name
    if ($projectName -isnot [string] -or $projectName -cnotmatch '^[A-Za-z][A-Za-z0-9_]{0,63}$' -or
        $projectDescriptor.project -cne "$projectName.vcxproj" -or $projectDescriptor.solution -cne "$projectName.slnx") {
        throw 'Invalid game project descriptor.'
    }
    if ($projectDescriptor.PSObject.Properties.Name -contains 'projectId') {
        if ($projectDescriptor.projectId -isnot [string] -or $projectDescriptor.projectId -cnotmatch '^[a-f0-9]{32}$') { throw 'Invalid project ID.' }
    }
    if ($projectDescriptor.PSObject.Properties.Name -contains 'generatedProjects') {
        if ($projectDescriptor.generatedProjects -isnot [bool]) { throw 'Invalid generated project flag.' }
    }
    $projectEngine = Get-ProjectEngineRoot -Content $projectRoot -Descriptor $projectDescriptor
    if ($RequireEngineRoot -and ![string]::Equals($projectEngine, [IO.Path]::GetFullPath($RequireEngineRoot).TrimEnd('\', '/'), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Game project uses a different engine checkout.'
    }
    return [pscustomobject]@{
        root = $projectRoot
        engineRoot = $projectEngine
        name = $projectName
        projectId = $projectDescriptor.projectId
        generatedProjects = $projectDescriptor.generatedProjects -eq $true
        descriptor = $projectDescriptor
    }
}

function Sync-GeneratedGameProject {
    param([object]$Context)
    if (!$Context.generatedProjects) { throw 'This project owns its build files; automatic regeneration is disabled.' }
    if ($Context.projectId -cnotmatch '^[a-f0-9]{32}$') { throw 'Generated projects require a stable project ID.' }
    $projectRoot = [Security.SecurityElement]::Escape($Context.engineRoot + '\')
    $projectGuid = [Guid]::ParseExact($Context.projectId, 'N').ToString().ToUpperInvariant()
    $projectName = $Context.name
    $projectText = @'
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <!-- WP1.project.json から生成します。 -->
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
    $solutionText = @'
<Solution>
  <!-- WP1.project.json から生成します。 -->
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
    $projectFiles = @{
        "$projectName.vcxproj" = $projectText.Replace('__NAME__', $projectName).Replace('__GUID__', $projectGuid).Replace('__ROOT__', $projectRoot)
        "$projectName.slnx" = $solutionText.Replace('__NAME__', $projectName).Replace('__ROOT__', $projectRoot.Replace('\', '/'))
    }
    foreach ($projectFile in $projectFiles.Keys) {
        $projectPath = Join-Path $Context.root $projectFile
        $projectSource = $projectFiles[$projectFile].Replace("`r`n", "`n").Replace("`n", "`r`n") + "`r`n"
        if ((Test-Path -LiteralPath $projectPath) -and [IO.File]::ReadAllText($projectPath) -ceq $projectSource) { continue }
        $projectTemporary = $projectPath + '.tmp.' + [Guid]::NewGuid().ToString('N')
        try {
            [IO.File]::WriteAllText($projectTemporary, $projectSource, [Text.UTF8Encoding]::new($false))
            if (Test-Path -LiteralPath $projectPath) { [IO.File]::Replace($projectTemporary, $projectPath, [NullString]::Value) }
            else { [IO.File]::Move($projectTemporary, $projectPath) }
        } finally {
            if (Test-Path -LiteralPath $projectTemporary) { Remove-Item -LiteralPath $projectTemporary }
        }
    }
}

function Write-RuntimeProjectContext {
    param([string]$Content, [string]$Destination)
    $runtimeIdentity = $null
    $runtimeDescriptor = Join-Path $Content 'WP1.project.json'
    $runtimeSource = Join-Path $Content 'WP1.runtime.json'
    if (Test-Path -LiteralPath $runtimeDescriptor) {
        $runtimeProject = Get-Content -LiteralPath $runtimeDescriptor -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($runtimeProject.PSObject.Properties.Name -contains 'projectId') {
            $runtimeIdentity = (Get-ProjectContext -Project $Content).projectId
        }
    } elseif (Test-Path -LiteralPath $runtimeSource) {
        $runtimeDocument = Get-Content -LiteralPath $runtimeSource -Raw -Encoding UTF8 | ConvertFrom-Json
        if (($runtimeDocument.version -isnot [int] -and $runtimeDocument.version -isnot [long]) -or $runtimeDocument.version -ne 1) {
            throw 'Unsupported runtime project context.'
        }
        $runtimeIdentity = $runtimeDocument.projectId
        if ($runtimeIdentity -isnot [string] -or $runtimeIdentity -cnotmatch '^[a-f0-9]{32}$') { throw 'Invalid runtime project ID.' }
    }
    $runtimeTarget = Join-Path $Destination 'WP1.runtime.json'
    if (!$runtimeIdentity) {
        if (Test-Path -LiteralPath $runtimeTarget) { Remove-Item -LiteralPath $runtimeTarget }
        return
    }
    if ($runtimeIdentity -isnot [string] -or $runtimeIdentity -cnotmatch '^[a-f0-9]{32}$') { throw 'Invalid runtime project ID.' }
    $runtimeText = ([ordered]@{version = 1; projectId = $runtimeIdentity} | ConvertTo-Json) + "`r`n"
    if ((Test-Path -LiteralPath $runtimeTarget) -and [IO.File]::ReadAllText($runtimeTarget) -ceq $runtimeText) { return }
    $runtimeTemporary = $runtimeTarget + '.tmp.' + [Guid]::NewGuid().ToString('N')
    try {
        [IO.File]::WriteAllText($runtimeTemporary, $runtimeText, [Text.UTF8Encoding]::new($false))
        if (Test-Path -LiteralPath $runtimeTarget) { [IO.File]::Replace($runtimeTemporary, $runtimeTarget, [NullString]::Value) }
        else { [IO.File]::Move($runtimeTemporary, $runtimeTarget) }
    } finally {
        if (Test-Path -LiteralPath $runtimeTemporary) { Remove-Item -LiteralPath $runtimeTemporary }
    }
}
