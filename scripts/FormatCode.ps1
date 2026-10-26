param(
    [string[]]$Paths = @(),
    [string]$ClangFormat = 'clang-format',
    [switch]$Fix,
    [string]$RepositoryRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
$requiredVersion = '22.1.3'

Push-Location $repoRoot
try {
    if ($ClangFormat -eq 'clang-format' -and !(Get-Command $ClangFormat -ErrorAction SilentlyContinue)) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $candidates = @(& $vswhere -latest -products '*' -find 'VC/Tools/Llvm/x64/bin/clang-format.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Visual Studio の検索に失敗しました。' }
            if ($candidates.Count -gt 0) { $ClangFormat = $candidates[0] }
        }
    }

    $version = & $ClangFormat --version
    if ($LASTEXITCODE -ne 0 -or "$version" -notmatch "\bversion $([regex]::Escape($requiredVersion))\b") {
        throw "clang-format $requiredVersion が必要です。-ClangFormat で実行ファイルを指定できます。"
    }

    # Git 管理下の自作 C++ だけを対象にし、外部コードと生成物を除外します。
    $tracked = @(& git -c core.quotepath=false ls-files -- Engine Player Editor SceneRuntime Assets src tests Tools)
    if ($LASTEXITCODE -ne 0) { throw 'Git 管理ファイルの取得に失敗しました。' }
    # 明示指定では新規の管理予定ファイルも確認し、既定の一括対象は従来どおり管理済みに限定します。
    if ($Paths.Count -gt 0) {
        $tracked += @(& git -c core.quotepath=false ls-files --others --exclude-standard -- Engine Player Editor SceneRuntime Assets src tests Tools)
        if ($LASTEXITCODE -ne 0) { throw '新規ファイルの取得に失敗しました。' }
    }
    $eligible = @($tracked | Where-Object {
        $_ -match '\.(cpp|h|hpp|cc|cxx|hh|hxx)$' -and
        $_ -notmatch '(^|/)(externals|generated|Bin|obj|x64|Debug|Development|Release)/' -and
        $_ -notmatch '\.generated\.'
    })

    $targets = $eligible
    if ($Paths.Count -gt 0) {
        $targets = @($Paths | ForEach-Object {
            if ([IO.Path]::IsPathRooted($_)) {
                throw "リポジトリ相対パスで指定してください: $_"
            }
            $absolute = [IO.Path]::GetFullPath((Join-Path $repoRoot $_))
            if (!$absolute.StartsWith($repoRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw "リポジトリ外のパスは指定できません: $_"
            }
            $relative = $absolute.Substring($repoRoot.Length).TrimStart('\', '/').Replace('\', '/')
            if ($eligible -notcontains $relative) {
                throw "対象外のパスです。管理対象の自作 C++ ファイルをリポジトリ相対パスで指定してください: $_"
            }
            $relative
        } | Sort-Object -Unique)
    }
    if ($targets.Count -eq 0) { throw '対象ファイルがありません。' }

    $failed = 0
    foreach ($target in $targets) {
        $arguments = @('--style=file', '--fallback-style=none')
        if ($Fix) { $arguments += '-i' }
        else { $arguments += @('--dry-run', '--Werror', '--ferror-limit=1') }
        & $ClangFormat @arguments -- $target
        if ($LASTEXITCODE -ne 0) { $failed++ }
    }
    if ($failed -gt 0) { throw "$failed / $($targets.Count) ファイルで整形チェックまたは整形に失敗しました。" }
    Write-Host "$($targets.Count) ファイルの処理が完了しました。"
}
finally {
    Pop-Location
}
