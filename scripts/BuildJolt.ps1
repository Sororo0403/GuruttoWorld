param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$sourcePath=Join-Path $repoRoot 'generated/dependencies/jolt-src'
$buildPath=Join-Path $repoRoot 'generated/intermediate/jolt'
$outputPath=Join-Path $repoRoot 'generated/outputs/jolt'
$revision='e77f175595e64cb44218cc9d9d56fc365ad0e36a'
if (!(Test-Path -LiteralPath (Join-Path $sourcePath 'Build/CMakeLists.txt'))) {
    & git clone --depth 1 --branch v5.6.0 https://github.com/jrouwe/JoltPhysics.git $sourcePath
    if ($LASTEXITCODE -ne 0) { throw 'Jolt download failed.' }
}
$actualRevision=& git -C $sourcePath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $revision) { throw 'Unexpected Jolt revision.' }
& cmake -S (Join-Path $sourcePath 'Build') -B $buildPath -G 'Visual Studio 18 2026' -A x64 -T host=x64 `
    -DJPH_BUILD_SHARED_LIBS=OFF -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF -DINTERPROCEDURAL_OPTIMIZATION=OFF -DGENERATE_DEBUG_SYMBOLS=OFF '-DCMAKE_CXX_FLAGS=/Z7' `
    -DTARGET_UNIT_TESTS=OFF -DTARGET_HELLO_WORLD=OFF -DTARGET_PERFORMANCE_TEST=OFF -DTARGET_SAMPLES=OFF -DTARGET_VIEWER=OFF `
    -DENABLE_ALL_WARNINGS=OFF -DENABLE_INSTALL=OFF -DPROFILER_IN_DEBUG_AND_RELEASE=OFF -DDEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF `
    -DFLOATING_POINT_EXCEPTIONS_ENABLED=OFF -DCPP_EXCEPTIONS_ENABLED=ON -DCPP_RTTI_ENABLED=ON `
    -DJPH_USE_DX12=OFF -DJPH_USE_VK=OFF -DJPH_USE_MTL=OFF -DJPH_USE_CPU_COMPUTE=OFF `
    -DUSE_AVX=OFF -DUSE_AVX2=OFF -DUSE_AVX512=OFF -DUSE_SSE4_1=OFF -DUSE_SSE4_2=OFF `
    -DUSE_LZCNT=OFF -DUSE_TZCNT=OFF -DUSE_F16C=OFF -DUSE_FMADD=OFF `
    "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$outputPath"
if ($LASTEXITCODE -ne 0) { throw 'Jolt configure failed.' }
& cmake --build $buildPath --config $Configuration --target Jolt --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Jolt build failed.' }
[IO.File]::WriteAllText((Join-Path $buildPath ($Configuration+'.ready')),$revision)
