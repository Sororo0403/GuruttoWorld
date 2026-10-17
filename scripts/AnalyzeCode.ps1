param(
    [string]$Cppcheck = "cppcheck",
    [string]$Lizard = "lizard",
    [string]$Label = "latest",
    [ValidateRange(1,16)][int]$Jobs = 4,
    [ValidateRange(0,9999)][int]$MsvcVersion = 0
)

$ErrorActionPreference = "Stop"
if ($Label -notmatch '^[a-zA-Z0-9_-]+$') { throw "Label must contain only letters, numbers, underscores or hyphens." }
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if ($Cppcheck -eq "cppcheck" -and !(Get-Command $Cppcheck -ErrorAction SilentlyContinue)) {
        $Cppcheck = Join-Path $env:ProgramFiles "Cppcheck/cppcheck.exe"
    }
    if ($MsvcVersion -eq 0) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (!$installation) { throw 'MSVC tools are required; alternatively pass -MsvcVersion explicitly.' }
        $toolVersion = (Get-Content -LiteralPath (Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
        $compiler = Get-Item -LiteralPath (Join-Path $installation "VC/Tools/MSVC/$toolVersion/bin/Hostx64/x64/cl.exe")
        $version = [version]$compiler.VersionInfo.FileVersion
        $MsvcVersion = $version.Major * 100 + $version.Minor
    }
    Write-Host "MSVC analysis definition: _MSC_VER=$MsvcVersion"
    $output = "generated/analysis"
    New-Item -ItemType Directory -Force $output | Out-Null
    $sources = @(rg --files Engine App Editor SceneRuntime Games/GuruttoWorld/Assets/Scripts -g '*.cpp' -g '!**/externals/**' -g '!**/Bin/**')
    if ($LASTEXITCODE -ne 0) { throw "Source discovery failed." }
    $sources | Set-Content "$output/translation-units.txt" -Encoding ascii
    $arguments = @(
        '--language=c++', '--std=c++20', '--platform=win64',
        '--enable=warning,style,performance,portability', '--inline-suppr',
        '-j', "$Jobs",
        '--error-exitcode=1', '-U__clang__', '-U__GNUC__',
        '--suppress=*:Engine/externals/*', '--suppress=*:Editor/externals/*',
        '--suppress=*:generated/dependencies/*', '--suppress=*:generated/intermediate/assimp/*',
        "-D_MSC_VER=$MsvcVersion", '-D_MSVC_LANG=202002L', '-D__cplusplus=199711L',
        '-D_WIN32', '-D_WIN64', '-D_WINDOWS', '-DJPH_OBJECT_STREAM',
        '-IEngine/include', '-ISceneRuntime/include', '-IEngine/externals/imgui',
        '-IEditor/externals/ImGuizmo', '-Igenerated/dependencies/assimp-src/include',
        '-Igenerated/intermediate/assimp/include', '-Igenerated/dependencies/jolt-src',
        "--file-list=$output/translation-units.txt"
    )
    # Windows PowerShell は、リダイレクトされた外部コマンドの診断をエラーレコードとして扱う場合があります。
    $ErrorActionPreference = "Continue"
    $cppcheckExit = 0
    foreach ($configuration in @('Debug','Development','Release')) {
        $cache = "$output/cppcheck-cache-$configuration"
        New-Item -ItemType Directory -Force $cache | Out-Null
        $defines = switch ($configuration) {
            Debug { @('-D_DEBUG','-DJPH_DEBUG','-D_ITERATOR_DEBUG_LEVEL=2','-UNDEBUG','-UENGINE_DEVELOPMENT') }
            Development { @('-DNDEBUG','-DENGINE_DEVELOPMENT','-D_ITERATOR_DEBUG_LEVEL=0','-U_DEBUG','-UJPH_DEBUG') }
            Release { @('-DNDEBUG','-D_ITERATOR_DEBUG_LEVEL=0','-U_DEBUG','-UJPH_DEBUG','-UENGINE_DEVELOPMENT') }
        }
        & $Cppcheck @arguments @defines "--cppcheck-build-dir=$cache" > "$output/cppcheck-$Label-$configuration.stdout.txt" 2> "$output/cppcheck-$Label-$configuration.txt"
        if ($LASTEXITCODE -ne 0) { $cppcheckExit = $LASTEXITCODE }
        Write-Host "$configuration cppcheck exit: $LASTEXITCODE"
    }
    $ErrorActionPreference = "Stop"
    & $Lizard -C 15 -L 100 -a 8 -x '*/externals/*' Engine App Editor SceneRuntime Games/GuruttoWorld/Assets/Scripts > "$output/lizard-$Label.txt"
    $lizardExit = $LASTEXITCODE
    Write-Host "cppcheck exit: $cppcheckExit; lizard exit: $lizardExit"
    Write-Host "Reports: $root/$output/*-$Label*.txt"
    if ($cppcheckExit -ne 0) { throw "Cppcheck found diagnostics; inspect the reports." }
    if ($lizardExit -ne 0) { throw "Cppcheck completed without diagnostics; Lizard complexity thresholds are exceeded. Inspect the complexity report." }
}
finally {
    Pop-Location
}
