param(
    [string]$Cppcheck = "cppcheck",
    [string]$Lizard = "lizard",
    [string]$Label = "latest"
)

$ErrorActionPreference = "Stop"
if ($Label -notmatch '^[a-zA-Z0-9_-]+$') { throw "Label must contain only letters, numbers, underscores or hyphens." }
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if ($Cppcheck -eq "cppcheck" -and !(Get-Command $Cppcheck -ErrorAction SilentlyContinue)) {
        $Cppcheck = Join-Path $env:ProgramFiles "Cppcheck/cppcheck.exe"
    }
    $output = "generated/analysis"
    New-Item -ItemType Directory -Force $output | Out-Null
    $sources = @(rg --files Engine App Editor SceneRuntime -g '*.cpp' -g '!**/externals/**')
    if ($LASTEXITCODE -ne 0) { throw "Source discovery failed." }
    $sources | Set-Content "$output/translation-units.txt" -Encoding ascii
    $arguments = @(
        '--language=c++', '--std=c++20', '--platform=win64',
        '--enable=warning,style,performance,portability', '--inline-suppr', '--quiet',
        '--error-exitcode=1', '-U__clang__', '-U__GNUC__',
        '--suppress=*:Engine/externals/*', '--suppress=*:Editor/externals/*',
        '-IEngine/include', '-ISceneRuntime/include', '-IEngine/externals/imgui',
        '-IEditor/externals/ImGuizmo', '-IEngine/externals/assimp/include',
        "--file-list=$output/translation-units.txt"
    )
    # Windows PowerShell may treat redirected native diagnostics as error records.
    $ErrorActionPreference = "Continue"
    & $Cppcheck @arguments > "$output/cppcheck-$Label.stdout.txt" 2> "$output/cppcheck-$Label.txt"
    $cppcheckExit = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    & $Lizard -C 15 -L 100 -a 8 -x '*/externals/*' Engine App Editor SceneRuntime > "$output/lizard-$Label.txt"
    $lizardExit = $LASTEXITCODE
    Write-Host "cppcheck exit: $cppcheckExit; lizard exit: $lizardExit"
    Write-Host "Reports: $root/$output/*-$Label.txt"
    if ($cppcheckExit -ne 0 -or $lizardExit -ne 0) { throw "Code analysis failed; inspect the reports." }
}
finally {
    Pop-Location
}
