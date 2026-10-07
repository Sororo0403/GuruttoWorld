param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$sourcePath = Join-Path $repoRoot 'generated/dependencies/assimp-src'
$buildPath = Join-Path $repoRoot 'generated/intermediate/assimp'
$outputPath = Join-Path $repoRoot 'generated/outputs/assimp'
$revision = 'e0b52347c6e52de2827ec957a9ebf00ce3c54f79'
if (!(Test-Path -LiteralPath (Join-Path $sourcePath 'CMakeLists.txt'))) {
    & git clone --depth 1 --branch v6.0.4 https://github.com/assimp/assimp.git $sourcePath
    if ($LASTEXITCODE -ne 0) { throw 'Assimp download failed.' }
}
$actualRevision = & git -C $sourcePath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $revision) { throw 'Unexpected Assimp revision.' }
& cmake -S $sourcePath -B $buildPath -G 'Visual Studio 18 2026' -A x64 `
    -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF `
    -DASSIMP_INSTALL=OFF -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF -DASSIMP_BUILD_OBJ_IMPORTER=ON -DASSIMP_BUILD_GLTF_IMPORTER=ON `
    -DASSIMP_NO_EXPORT=ON -DASSIMP_BUILD_ZLIB=ON -DASSIMP_WARNINGS_AS_ERRORS=OFF `
    -DASSIMP_INJECT_DEBUG_POSTFIX=OFF '-DLIBRARY_SUFFIX=' '-DCMAKE_CXX_FLAGS=/EHsc' `
    "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$outputPath"
if ($LASTEXITCODE -ne 0) { throw 'Assimp configure failed.' }
& cmake --build $buildPath --config $Configuration --target assimp --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Assimp build failed.' }
[IO.File]::WriteAllText((Join-Path $buildPath ($Configuration + '.ready')), $revision)
