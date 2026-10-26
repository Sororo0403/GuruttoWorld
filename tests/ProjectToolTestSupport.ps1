# テストから製品と同じネイティブCLIを呼び出します。
function Get-ProjectTool {
    if ($env:WP1_PROJECT_TOOL) {
        if (!(Test-Path -LiteralPath $env:WP1_PROJECT_TOOL -PathType Leaf)) { throw 'WP1_PROJECT_TOOL does not exist.' }
        return $env:WP1_PROJECT_TOOL
    }
    $toolRoot = Split-Path $PSScriptRoot -Parent
    $tool = Join-Path $toolRoot 'generated/outputs/x64/Development/ProjectTool/ProjectTool.exe'
    if (!(Test-Path -LiteralPath $tool)) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $installation = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
        if (!$installation) { throw 'Visual Studio with MSBuild is required.' }
        & (Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe') (Join-Path $toolRoot 'Tools/ProjectTool/ProjectTool.vcxproj') /p:Configuration=Development /p:Platform=x64 /p:PreferredToolArchitecture=x64 /m:1 /nr:false /v:minimal /nologo | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'ProjectTool build failed.' }
    }
    return $tool
}

function Invoke-ProjectTool {
    param([Parameter(Position = 0)][string]$Command, [Parameter(ValueFromRemainingArguments = $true)][object[]]$ToolArguments)
    $output = & (Get-ProjectTool) $Command @ToolArguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($output -join "`n") }
    return $output
}

function Get-ProjectContext {
    param([string]$Project, [string]$RequireEngineRoot)
    $contextRoot = [IO.Path]::GetFullPath($Project)
    if ([IO.Path]::GetFileName($contextRoot) -eq 'WP1.project.json') { $contextRoot = Split-Path $contextRoot -Parent }
    $toolEngine = if ($RequireEngineRoot) { $RequireEngineRoot } else { Split-Path $PSScriptRoot -Parent }
    $descriptor = (Invoke-ProjectTool project-context -Project $contextRoot -Engine $toolEngine) -join "`n" | ConvertFrom-Json
    return [pscustomobject]@{
        root = $contextRoot
        engineRoot = $descriptor.engineRoot
        name = $descriptor.name
        projectId = $descriptor.projectId
        generatedProjects = $descriptor.generatedProjects -eq $true
        descriptor = $descriptor
    }
}

function Sync-GeneratedGameProject {
    param([object]$Context)
    Invoke-ProjectTool sync-project -Project $Context.root -Engine $Context.engineRoot | Out-Null
}

function Copy-BuildContentSnapshot {
    param([string]$Content, [string]$Destination, [bool]$IncludeEngineResources = $true)
    $toolArguments = @('-Content', $Content, '-Destination', $Destination)
    if (!$IncludeEngineResources) { $toolArguments += '--no-engine-resources' }
    Invoke-ProjectTool snapshot @toolArguments | Out-Null
}
