param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$sourcePath = Join-Path $repoRoot 'generated/dependencies/directxtex-src'
$buildPath = Join-Path $repoRoot 'generated/intermediate/directxtex'
$outputPath = Join-Path $repoRoot 'generated/outputs/directxtex'
$revision = '4feb3e11a020f35b796fc769a74216a555d4f5ef'
if (!(Test-Path -LiteralPath (Join-Path $sourcePath 'CMakeLists.txt'))) {
    & git clone --depth 1 --branch may2026 https://github.com/microsoft/DirectXTex.git $sourcePath
    if ($LASTEXITCODE -ne 0) { throw 'DirectXTex download failed.' }
}
$actualRevision = & git -C $sourcePath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $revision) { throw 'Unexpected DirectXTex revision.' }
& cmake -S $sourcePath -B $buildPath -G 'Visual Studio 18 2026' -A x64 `
    -DBUILD_SHARED_LIBS=OFF -DBUILD_TOOLS=OFF -DBUILD_SAMPLE=OFF -DBUILD_DX11=OFF -DBUILD_DX12=OFF -DBC_USE_OPENMP=OFF `
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL' `
    "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG=$outputPath/Debug" "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE=$outputPath/Release"
if ($LASTEXITCODE -ne 0) { throw 'DirectXTex configure failed.' }
& cmake --build $buildPath --config $Configuration --target DirectXTex --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'DirectXTex build failed.' }
[IO.File]::WriteAllText((Join-Path $buildPath ($Configuration + '.ready')), $revision)
