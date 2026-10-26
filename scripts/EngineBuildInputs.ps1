function Get-EngineBuildInputFiles {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [switch]$IncludeDependencies
    )

    $folders = @('Engine/include', 'Engine/src', 'Engine/build', 'Engine/externals',
        'SceneRuntime/include', 'SceneRuntime/src', 'SceneRuntime/build', 'Tools/ProjectTool')
    if ($IncludeDependencies) {
        $folders += @('generated/dependencies/jolt-src/Jolt', 'generated/dependencies/assimp-src/include',
            'generated/dependencies/assimp-src/code', 'generated/dependencies/assimp-src/contrib',
            'generated/dependencies/directxtex-src/DirectXTex', 'generated/dependencies/recast-src/Recast',
            'generated/dependencies/recast-src/Detour')
    }
    $files = @()
    foreach ($folder in $folders) {
        $path = Join-Path $Root $folder
        if (!(Test-Path -LiteralPath $path)) { continue }
        $files += @(Get-ChildItem -LiteralPath $path -File -Recurse | Where-Object {
            $_.Extension -in @('.h', '.hpp', '.inl', '.c', '.cpp', '.targets', '.props')
        })
    }
    foreach ($relative in @('Engine/Engine.vcxproj', 'Engine/EngineCore.vcxproj', 'SceneRuntime/SceneRuntimeCore.vcxproj', 'SceneRuntime/SceneRuntime.vcxproj', 'SceneRuntime/SceneData.vcxproj', 'SceneRuntime/SceneStorage.vcxproj',
        'Tools/ProjectTool/ProjectTool.vcxproj', 'scripts/GenerateEngineIdentity.ps1', 'scripts/EngineBuildInputs.ps1',
        'scripts/BuildAssimp.ps1', 'scripts/BuildJolt.ps1', 'scripts/BuildDirectXTex.ps1', 'scripts/BuildRecast.ps1')) {
        $path = Join-Path $Root $relative
        if (Test-Path -LiteralPath $path -PathType Leaf) { $files += Get-Item -LiteralPath $path }
    }
    return @($files | Sort-Object FullName -Unique)
}
