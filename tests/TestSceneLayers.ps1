$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskProjects = @{}
foreach ($name in @('SceneData', 'SceneStorage', 'SceneRuntimeCore', 'SceneRuntime')) {
    $path = Join-Path $taskRoot "SceneRuntime/$name.vcxproj"
    $taskProjects[$name] = [xml](Get-Content -LiteralPath $path -Raw)
}
$taskProjects.EngineCore = [xml](Get-Content -LiteralPath (Join-Path $taskRoot 'Engine/EngineCore.vcxproj') -Raw)
$taskDataReferences = @($taskProjects.SceneData.SelectNodes('//*[local-name()="ProjectReference"]') |
    ForEach-Object { [IO.Path]::GetFileName($_.Include) })
if (($taskDataReferences -join ',') -ne 'EngineCore.vcxproj' -or
    @($taskProjects.EngineCore.SelectNodes('//*[local-name()="ProjectReference"]')).Count) {
    throw 'SceneData may reference only the dependency-free EngineCore target.'
}
$taskStorageReferences = @($taskProjects.SceneStorage.SelectNodes('//*[local-name()="ProjectReference"]') |
    ForEach-Object { [IO.Path]::GetFileName($_.Include) } | Sort-Object)
if (($taskStorageReferences -join ',') -ne 'Engine.vcxproj,SceneData.vcxproj') {
    throw 'SceneStorage may reference only Engine and SceneData.'
}
$taskOwners = @{}
$taskRuntimeReferences = @($taskProjects.SceneRuntimeCore.SelectNodes('//*[local-name()="ProjectReference"]') |
    ForEach-Object { [IO.Path]::GetFileName($_.Include) })
if (($taskRuntimeReferences -join ',') -ne 'SceneData.vcxproj') {
    throw 'SceneRuntimeCore may reference only SceneData and its CPU dependencies.'
}
foreach ($name in $taskProjects.Keys) {
    foreach ($source in $taskProjects[$name].SelectNodes('//*[local-name()="ClCompile"][@Include]')) {
        if ($taskOwners.ContainsKey($source.Include)) { throw "Duplicate source ownership: $($source.Include)" }
        $taskOwners[$source.Include] = $name
    }
}
$taskVisited = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$taskPending = [Collections.Generic.Queue[string]]::new()
foreach ($source in $taskProjects.SceneData.SelectNodes('//*[local-name()="ClCompile"][@Include]')) {
    $taskPending.Enqueue((Join-Path $taskRoot "SceneRuntime/$($source.Include)"))
}
foreach ($source in $taskProjects.EngineCore.SelectNodes('//*[local-name()="ClCompile"][@Include]')) {
    $taskPending.Enqueue((Join-Path $taskRoot "Engine/$($source.Include)"))
}
foreach ($source in $taskProjects.SceneRuntimeCore.SelectNodes('//*[local-name()="ClCompile"][@Include]')) {
    $taskPending.Enqueue((Join-Path $taskRoot "SceneRuntime/$($source.Include)"))
}
while ($taskPending.Count) {
    $path = [IO.Path]::GetFullPath($taskPending.Dequeue())
    if (!$taskVisited.Add($path)) { continue }
    $source = Get-Content -LiteralPath $path -Raw
    if ($source -match 'std::filesystem::(exists|is_directory|is_regular_file|recursive_directory_iterator|directory_iterator|canonical|weakly_canonical|copy|remove|rename|create_directories)\s*\(') {
        throw "SceneData performs filesystem I/O from $path"
    }
    foreach ($include in [regex]::Matches($source, '(?m)^\s*#include\s*[<"]([^>"]+)[>"]')) {
        $name = $include.Groups[1].Value.Replace('\', '/')
        if ($name -match '^(fstream|Windows\.h|d3d\w*\.h|SceneRuntime/(?:[^/]+/)*(SceneWorld|SceneUi|PhysicsWorld|ScriptRuntime|SceneEnvironment|MaterialAsset)\.h|Engine/Core/(FileContents|FileReplacement)\.h|Engine/(Platform|Audio)/|Engine/Assets/(AssetDatabase|ContentContext|AssetReferenceResolver)\.h|Engine/Graphics/(DirectX12|Renderers|Resources)/)') {
            throw "SceneData includes forbidden dependency $name from $path"
        }
        foreach ($candidate in @((Join-Path (Split-Path $path -Parent) $name),
                (Join-Path $taskRoot "SceneRuntime/include/$name"), (Join-Path $taskRoot "Engine/include/$name"))) {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $taskPending.Enqueue($candidate)
                break
            }
        }
    }
}
Write-Output "PASS: SceneData $($taskVisited.Count) source/header dependencies, project directions and unique source ownership"
