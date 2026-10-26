param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$sourcePath = Join-Path $repoRoot 'generated/dependencies/recast-src'
$buildPath = Join-Path $repoRoot 'generated/intermediate/recast'
$outputPath = Join-Path $repoRoot 'generated/outputs/recast'
$revision = '6dc1667f580357e8a2154c28b7867bea7e8ad3a7'
if (!(Test-Path -LiteralPath (Join-Path $sourcePath 'CMakeLists.txt'))) {
    & git clone --depth 1 --branch v1.6.0 https://github.com/recastnavigation/recastnavigation.git $sourcePath
    if ($LASTEXITCODE -ne 0) { throw 'Recast download failed.' }
}
$actualRevision = & git -C $sourcePath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $revision) { throw 'Unexpected Recast revision.' }
& cmake -S $sourcePath -B $buildPath -G 'Visual Studio 18 2026' -A x64 `
    -DBUILD_SHARED_LIBS=OFF -DRECASTNAVIGATION_DEMO=OFF -DRECASTNAVIGATION_TESTS=OFF -DRECASTNAVIGATION_EXAMPLES=OFF `
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL' `
    "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$outputPath"
if ($LASTEXITCODE -ne 0) { throw 'Recast configure failed.' }
& cmake --build $buildPath --config $Configuration --target Recast Detour --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Recast build failed.' }
[IO.File]::WriteAllText((Join-Path $buildPath ($Configuration + '.ready')), $revision)
