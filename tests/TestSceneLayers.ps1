$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskProjects = @{}
foreach ($name in @('SceneData', 'SceneStorage', 'SceneRuntimeCore', 'SceneRendering', 'SceneSimulation', 'SceneRuntime', 'SceneTitle')) {
    $path = Join-Path $taskRoot "SceneRuntime/$name.vcxproj"
    $taskProjects[$name] = [xml](Get-Content -LiteralPath $path -Raw)
}
$taskProjects.EngineCore = [xml](Get-Content -LiteralPath (Join-Path $taskRoot 'Engine/EngineCore.vcxproj') -Raw)
$taskProjects.EngineAssets = [xml](Get-Content -LiteralPath (Join-Path $taskRoot 'Engine/EngineAssets.vcxproj') -Raw)
$taskProjects.EnginePlatform = [xml](Get-Content -LiteralPath (Join-Path $taskRoot 'Engine/EnginePlatform.vcxproj') -Raw)
$taskProjects.Engine = [xml](Get-Content -LiteralPath (Join-Path $taskRoot 'Engine/Engine.vcxproj') -Raw)
$taskDataReferences = @($taskProjects.SceneData.SelectNodes('//*[local-name()="ProjectReference"]') |
    ForEach-Object { [IO.Path]::GetFileName($_.Include) })
if (($taskDataReferences -join ',') -ne 'EngineCore.vcxproj' -or
    @($taskProjects.EngineCore.SelectNodes('//*[local-name()="ProjectReference"]')).Count) {
    throw 'SceneData may reference only the dependency-free EngineCore target.'
}
$taskStorageReferences = @($taskProjects.SceneStorage.SelectNodes('//*[local-name()="ProjectReference"]') |
    ForEach-Object { [IO.Path]::GetFileName($_.Include) } | Sort-Object)
if (($taskStorageReferences -join ',') -ne 'EngineAssets.vcxproj,SceneData.vcxproj') {
    throw 'SceneStorage may reference only CPU EngineAssets and SceneData.'
}
$taskOwners = @{}
$taskRuntimeReferences = @($taskProjects.SceneRuntimeCore.SelectNodes('//*[local-name()="ProjectReference"]') |
    ForEach-Object { [IO.Path]::GetFileName($_.Include) })
if (($taskRuntimeReferences -join ',') -ne 'SceneData.vcxproj') {
    throw 'SceneRuntimeCore may reference only SceneData and its CPU dependencies.'
}
foreach ($name in $taskProjects.Keys) {
    foreach ($source in $taskProjects[$name].SelectNodes('//*[local-name()="ClCompile"][@Include]')) {
        $folder = if ($name -like 'Engine*') { 'Engine' } else { 'SceneRuntime' }
        $sourcePath = [IO.Path]::GetFullPath((Join-Path $taskRoot "$folder/$($source.Include)"))
        if ($taskOwners.ContainsKey($sourcePath)) { throw "Duplicate source ownership: $sourcePath" }
        $taskOwners[$sourcePath] = $name
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

$taskRenderReferences = @($taskProjects.SceneRendering.SelectNodes('//*[local-name()="ProjectReference"]') |
    ForEach-Object { [IO.Path]::GetFileName($_.Include) })
if ((($taskRenderReferences | Sort-Object) -join ',') -ne 'Engine.vcxproj,SceneData.vcxproj') {
    throw 'SceneRendering must depend only on GPU Engine facilities and the packet contract.'
}
$taskRenderVisited = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$taskRenderPending = [Collections.Generic.Queue[string]]::new()
foreach ($source in $taskProjects.SceneRendering.SelectNodes('//*[local-name()="ClCompile"][@Include]')) {
    $taskRenderPending.Enqueue((Join-Path $taskRoot "SceneRuntime/$($source.Include)"))
}
while ($taskRenderPending.Count) {
    $path = [IO.Path]::GetFullPath($taskRenderPending.Dequeue())
    if (!$taskRenderVisited.Add($path)) { continue }
    foreach ($include in [regex]::Matches((Get-Content -LiteralPath $path -Raw), '(?m)^\s*#include\s*[<"]([^>"]+)[>"]')) {
        $name = $include.Groups[1].Value.Replace('\', '/')
        if ($name -match '^SceneRuntime/(?:[^/]+/)*(SceneWorld|SceneLayout|SceneEnvironment|WorldState|WorldDefinition|RuntimeSession|RuntimeScripts|ScriptRuntime|ScriptApi|PhysicsWorld)\.h$') {
            throw "Rendering includes mutable world or authoring dependency $name from $path"
        }
        foreach ($candidate in @((Join-Path (Split-Path $path -Parent) $name),
                (Join-Path $taskRoot "SceneRuntime/include/$name"), (Join-Path $taskRoot "Engine/include/$name"))) {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $taskRenderPending.Enqueue($candidate)
                break
            }
        }
    }
}
Write-Output "PASS: SceneRendering $($taskRenderVisited.Count) source/header dependencies exclude authoring, mutable world and Script APIs"

foreach ($entry in @(@('EnginePlatform', 'EngineCore.vcxproj'),
        @('EngineAssets', 'EngineCore.vcxproj,EnginePlatform.vcxproj'),
        @('SceneSimulation', 'EngineAssets.vcxproj,SceneRuntimeCore.vcxproj,SceneStorage.vcxproj'))) {
    $references = @($taskProjects[$entry[0]].SelectNodes('//*[local-name()="ProjectReference"]') |
        ForEach-Object { [IO.Path]::GetFileName($_.Include) } | Sort-Object)
    if (($references -join ',') -ne $entry[1]) { throw "Unexpected CPU dependencies for $($entry[0])" }
}
$taskCpuPending = [Collections.Generic.Queue[string]]::new()
$taskCpuVisited = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($name in @('EnginePlatform', 'EngineAssets', 'SceneSimulation', 'SceneStorage')) {
    $folder = if ($name -like 'Engine*') { 'Engine' } else { 'SceneRuntime' }
    foreach ($source in $taskProjects[$name].SelectNodes('//*[local-name()="ClCompile"][@Include]')) {
        $taskCpuPending.Enqueue((Join-Path $taskRoot "$folder/$($source.Include)"))
    }
}
while ($taskCpuPending.Count) {
    $path = [IO.Path]::GetFullPath($taskCpuPending.Dequeue())
    if (!$taskCpuVisited.Add($path)) { continue }
    foreach ($include in [regex]::Matches((Get-Content -LiteralPath $path -Raw), '(?m)^\s*#include\s*[<"]([^>"]+)[>"]')) {
        $name = $include.Groups[1].Value.Replace('\', '/')
        if ($name -match '^(SceneRuntime/(?:[^/]+/)*(SceneWorld|SceneEnvironment|SceneUi|ScenePresentation|RenderScene|RenderPresentation)\.h|Engine/Graphics/(DirectX12|Renderers|Resources)/|d3d12\.h)') {
            throw "CPU subsystem includes rendering dependency $name from $path"
        }
        foreach ($candidate in @((Join-Path (Split-Path $path -Parent) $name),
                (Join-Path $taskRoot "SceneRuntime/include/$name"), (Join-Path $taskRoot "Engine/include/$name"))) {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $taskCpuPending.Enqueue($candidate)
                break
            }
        }
    }
}
Write-Output "PASS: CPU assets/platform/storage/simulation $($taskCpuVisited.Count) source/header dependencies exclude rendering"

# 共通再生と配布SDKは、任意のTitle実装・テスト用の旧Script APIを参照しません。
foreach ($name in @('SceneRuntime', 'SceneSimulation', 'SceneRuntimeCore')) {
    foreach ($source in $taskProjects[$name].SelectNodes('//*[local-name()="ClCompile"][@Include]')) {
        $path = Join-Path $taskRoot "SceneRuntime/$($source.Include)"
        $text = Get-Content -LiteralPath $path -Raw
        if ($text -match 'LegacyScripts/|Scripts/(ScriptRuntime|ScriptApi|LegacyScriptAdapter)\.h|Gameplay/Title(?:Bindings|Controller|Menu|Ui)\.h') {
            throw "Common runtime includes retired or optional API: $path"
        }
    }
}
if (Select-String -LiteralPath (Join-Path $taskRoot 'SceneRuntime/include/SceneRuntime/Gameplay/GamePlayback.h') -Pattern 'TitleController|TitleBindings') {
    throw 'Common playback owns an optional Title implementation.'
}
Write-Output 'PASS: optional Title and test-only legacy Script APIs are absent from common runtime sources'

# 編集表示とGPU準備はホストのWorldや実行セッションを取り込みません。
foreach ($relative in @('src/Rendering/EditorProjection.cpp', 'src/Rendering/SceneGpuResources.cpp',
        'src/Rendering/SceneRenderBindings.h', 'include/SceneRuntime/Rendering/EditorProjection.h',
        'include/SceneRuntime/Rendering/SceneGpuResources.h', 'include/SceneRuntime/Rendering/SceneResourcePreparation.h',
        'src/Rendering/SceneRenderState.cpp', 'include/SceneRuntime/Rendering/SceneRenderState.h')) {
    $path = Join-Path $taskRoot "SceneRuntime/$relative"
    $text = Get-Content -LiteralPath $path -Raw
    if ($text -match '#include\s*[<"][^>"\r\n]*(SceneWorld|SceneWorldRuntime|RuntimeSession|RuntimeScripts|ScriptRuntime)\.h[>"]') {
        throw "Preparation adapter includes its host or runtime session: $path"
    }
    if ($relative -like '*.cpp' -and $taskOwners[[IO.Path]::GetFullPath($path)] -ne 'SceneRuntime') {
        throw "Preparation adapter must be owned by SceneRuntime: $path"
    }
}
Write-Output 'PASS: editor projection, GPU preparation and render state are independent of SceneWorld and RuntimeSession'

# 文書と履歴の調停は、GPUや実行ホストを取り込まないCPUの契約です。
foreach ($relative in @('Editor/src/Documents/DocumentSession.h', 'Editor/src/Documents/DocumentSession.cpp',
        'SceneRuntime/include/SceneRuntime/Scene/SceneDocumentProjection.h')) {
    $path = Join-Path $taskRoot $relative
    $text = Get-Content -LiteralPath $path -Raw
    if ($text -match '#include\s*[<"][^>"\r\n]*(SceneWorld|RuntimeSession|SceneRenderState|SceneGpuResources)\.h[>"]') {
        throw "Document session includes a rendering or runtime host: $path"
    }
}
$taskDocumentHeader = Get-Content -LiteralPath (Join-Path $taskRoot 'SceneRuntime/include/SceneRuntime/Scene/SceneDocumentData.h') -Raw
if ($taskDocumentHeader -match '\bSceneWorld\b') {
    throw 'CPU document data refers to a concrete SceneWorld.'
}
$taskEditorMain = Get-Content -LiteralPath (Join-Path $taskRoot 'Editor/src/main.cpp') -Raw
if ($taskEditorMain -cmatch '\bhistory\.(Observe|Reset|Commit|Applied|Saved|RecordTransaction)\s*\(') {
    throw 'Editor main mutates history outside its DocumentSession.'
}
Write-Output 'PASS: document sessions use CPU projection contracts and own editor history changes'
