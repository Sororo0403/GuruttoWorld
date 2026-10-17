param(
    [Parameter(Mandatory = $true)][string]$Content,
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$FallbackContent
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'FileHash.ps1')
. (Join-Path $PSScriptRoot 'ContentLayers.ps1')

function Get-FullDirectory([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -eq [IO.Path]::GetPathRoot($full)) { return $full }
    return $full.TrimEnd('\', '/')
}

function Test-ContainsPath([string]$Parent, [string]$Child) {
    return $Parent.Equals($Child, [StringComparison]::OrdinalIgnoreCase) -or
        $Child.StartsWith($Parent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-NoLinkedParent([string]$Path) {
    for ($current = $Path; $current; $current = Split-Path $current -Parent) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Content synchronization does not follow links: $current"
            }
        }
    }
}

function Get-ContentFiles([string]$Root) {
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($Root)
    while ($pending.Count -gt 0) {
        foreach ($entry in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Content synchronization does not follow links: $($entry.FullName)"
            }
            if ($entry.PSIsContainer) {
                $pending.Push($entry.FullName)
            } else {
                $entry
            }
        }
    }
}

function Test-GeneratedContent([string]$Relative) {
    return $Relative.StartsWith('Assets/Scripts/Bin/', [StringComparison]::OrdinalIgnoreCase) -or
        $Relative.StartsWith('Shaders/Compiled/', [StringComparison]::OrdinalIgnoreCase)
}

$destinationRoot = Get-FullDirectory $Destination
if ($destinationRoot -eq [IO.Path]::GetPathRoot($destinationRoot)) {
    throw 'A volume root cannot be used as a content output directory.'
}
$sources = @()
$shared = Get-SharedContentRoot -Content $Content
Assert-SharedAssetIds -Shared $shared -Content $Content
if ($shared) { $sources += Get-FullDirectory $shared }
if ($FallbackContent) { $sources += Get-FullDirectory $FallbackContent }
$sources += Get-FullDirectory $Content
$inventory = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
$outputFiles = @()

# 削除前に全入力と出力の境界を確認し、素材の正本やリンク先へ書き込まない。
Assert-NoLinkedParent $destinationRoot
foreach ($source in $sources) {
    Assert-NoLinkedParent $source
    foreach ($folder in @('Assets', 'Shaders')) {
        $sourceFolder = Join-Path $source $folder
        if (!(Test-Path -LiteralPath $sourceFolder -PathType Container)) {
            if ($folder -eq 'Shaders' -and $source -eq (Get-FullDirectory $Content) -and $shared) { continue }
            throw "Content folder is missing: $sourceFolder"
        }
        foreach ($outputFolder in @('Assets', 'Shaders')) {
            $target = Join-Path $destinationRoot $outputFolder
            if ((Test-ContainsPath $sourceFolder $destinationRoot) -or
                (Test-ContainsPath $target $sourceFolder)) {
                throw 'Source and destination content folders must not overlap.'
            }
        }
        Assert-NoLinkedParent $sourceFolder
        foreach ($file in Get-ContentFiles $sourceFolder) {
            $relative = $folder + '/' + $file.FullName.Substring($sourceFolder.Length + 1).Replace('\', '/')
            if ((Test-GeneratedContent $relative) -or $file.Name -match '\.tmp($|\.)') { continue }
            # ゲーム側の同名素材を、プレイヤー共通素材より優先する。
            $inventory[$relative] = $file.FullName
        }
    }
}
foreach ($folder in @('Assets', 'Shaders')) {
    $target = Join-Path $destinationRoot $folder
    Assert-NoLinkedParent $target
    if (Test-Path -LiteralPath $target) { $outputFiles += @(Get-ContentFiles $target) }
}

foreach ($relative in $inventory.Keys) {
    $source = $inventory[$relative]
    $target = Join-Path $destinationRoot $relative
    $hash = Get-Wp1FileHash -LiteralPath $source
    if (!(Test-Path -LiteralPath $target -PathType Leaf) -or
        (Get-Wp1FileHash -LiteralPath $target) -ne $hash) {
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
        if ((Get-Wp1FileHash -LiteralPath $target) -ne $hash) {
            throw "Content changed while copying: $relative"
        }
    }
}

# Assets と Shaders はビルドが管理する。DLL とシェーダーキャッシュは生成側の管理なので保持する。
# 新規導入時にも以前のコピー結果を整理し、改名前の .meta による ID 重複を残さない。
foreach ($file in $outputFiles) {
    $relative = $file.FullName.Substring($destinationRoot.Length + 1).Replace('\', '/')
    if (!(Test-GeneratedContent $relative) -and !$inventory.ContainsKey($relative)) {
        Remove-Item -LiteralPath $file.FullName
    }
}
Write-Output "Synchronized $($inventory.Count) content files."
