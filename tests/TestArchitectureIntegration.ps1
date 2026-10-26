param(
    [Parameter(Mandatory = $true)][string]$Project,
    [string[]]$AdditionalProjects = @(),
    [ValidateSet('Debug', 'Development', 'Release')][string[]]$Configurations = @('Debug', 'Development', 'Release'),
    [ValidateSet('Debug', 'Release')][string]$RegressionConfiguration = 'Debug',
    [ValidateRange(1, 600)][int]$TimeoutSeconds = 120,
    [switch]$SkipGpu,
    [switch]$IncludeStateRegression,
    [switch]$SkipRegression
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'ProjectToolTestSupport.ps1')
$taskContext = Get-ProjectContext -Project $Project -RequireEngineRoot $taskRoot
if (!$taskContext.generatedProjects) { throw '検証用の生成プロジェクトを指定してください。' }
$taskAdditional = @($AdditionalProjects | ForEach-Object { Get-ProjectContext -Project $_ -RequireEngineRoot $taskRoot })
$taskNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
[void]$taskNames.Add($taskContext.name)
foreach ($context in $taskAdditional) {
    if (!$taskNames.Add($context.name)) { throw '検証するプロジェクト名が重複しています。' }
}
$taskLogs = Join-Path $taskRoot ('generated/design-migration/integration-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskLogs -Force | Out-Null
$taskResults = [Collections.Generic.List[object]]::new()

if (-not ('Wp1ArchitectureProcess' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Wp1ArchitectureProcess {
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetExitCodeProcess(IntPtr process, out uint code);
}
'@
}

function Invoke-ArchitectureProbe {
    param([string]$Name, [string]$Executable, [string]$Arguments = '', [string]$Mode = '')
    $previous = @(Get-ChildItem Env:WP1_*_ONLY)
    $setup = Get-Item Env:WP1_SCRIPT_MODULE_SETUP -ErrorAction SilentlyContinue
    if ($setup) { $previous += $setup }
    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    $process = $null
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        $temporary = Join-Path $taskRoot ('generated/tests/temp/' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $temporary -Force | Out-Null
        $env:TEMP = $temporary
        $env:TMP = $temporary
        foreach ($item in $previous) { Remove-Item ('Env:' + $item.Name) }
        if ($Mode) { Set-Item ('Env:' + $Mode) '1' }
        $parameters = @{
            FilePath = (Resolve-Path -LiteralPath $Executable).Path
            WorkingDirectory = $taskRoot
            WindowStyle = 'Hidden'
            PassThru = $true
            RedirectStandardOutput = (Join-Path $taskLogs ($Name + '-output.log'))
            RedirectStandardError = (Join-Path $taskLogs ($Name + '-error.log'))
        }
        if ($Arguments) { $parameters.ArgumentList = $Arguments }
        $process = Start-Process @parameters
        # ポーリングでProcessが更新される前にハンドルを保持し、終了コードを確実に取得します。
        $handle = $process.Handle
        while (!$process.WaitForExit(1000)) {
            if ($clock.Elapsed.TotalSeconds -gt $TimeoutSeconds) { throw "検証がタイムアウトしました: $Name" }
        }
        $process.WaitForExit()
        [uint32]$exitCode = 0
        if (![Wp1ArchitectureProcess]::GetExitCodeProcess($handle, [ref]$exitCode)) { throw "終了コードを取得できません: $Name" }
        $taskResults.Add([pscustomobject]@{ name = $Name; exitCode = $exitCode; seconds = $clock.Elapsed.TotalSeconds })
        if ($exitCode -ne 0) {
            Get-Content -LiteralPath $parameters.RedirectStandardError -Tail 15
            Get-Content -LiteralPath $parameters.RedirectStandardOutput -Tail 8
            throw "検証に失敗しました: $Name ($exitCode), $taskLogs"
        }
        Write-Output "PASS: $Name"
    } finally {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
        if ($process) {
            if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
            $process.Dispose()
        }
        if ($Mode) { Remove-Item ('Env:' + $Mode) -ErrorAction SilentlyContinue }
        foreach ($item in $previous) { Set-Item ('Env:' + $item.Name) $item.Value }
    }
}

try {
    foreach ($configuration in $Configurations) {
        $player = Join-Path $taskContext.root "generated/outputs/x64/$configuration/App/$($taskContext.name).exe"
        Invoke-ArchitectureProbe -Name "minimal-$configuration" -Executable $player -Arguments '--validate-package'
        if ($configuration -in @('Debug', 'Development')) {
            $editor = Join-Path $taskRoot "generated/outputs/x64/$configuration/Editor/Editor.exe"
            $arguments = '--validate-project --project "' + $taskContext.root + '"'
            Invoke-ArchitectureProbe -Name "editor-$configuration" -Executable $editor -Arguments $arguments
        }
        foreach ($context in $taskAdditional) {
            $player = Join-Path $context.root "generated/outputs/x64/$configuration/App/$($context.name).exe"
            Invoke-ArchitectureProbe -Name "player-$($context.name)-$configuration" -Executable $player -Arguments '--validate-package'
        }
    }
    if (!$SkipRegression) {
        $regression = Join-Path $taskRoot "generated/tests/outputs/x64/$RegressionConfiguration/ReviewRegressionValidation/ReviewRegressionValidation.exe"
        $modes = @('WP1_ASSET_ONLY', 'WP1_PREFAB_ONLY', 'WP1_SCRIPT_ONLY')
        if (!$SkipGpu) { $modes += @('WP1_SCRIPT_RUNTIME_ONLY', 'WP1_SCENE_COLLECTION_ONLY', 'WP1_PREFAB_RUNTIME_ONLY', 'WP1_OPTIONAL_FEATURES_ONLY', 'WP1_DOCUMENT_SESSION_ONLY') }
        if ($IncludeStateRegression) {
            $modes += 'WP1_AUTHORING_ONLY'
            if (!$SkipGpu) { $modes += @('WP1_FOLLOWUP_STATE_ONLY', 'WP1_SETTINGS_CAUSE_ONLY') }
        }
        foreach ($mode in $modes) {
            Invoke-ArchitectureProbe -Name $mode -Executable $regression -Mode $mode
        }
    }
} finally {
    $taskResults | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskLogs 'results.json') -Encoding UTF8
    Write-Output "Integration logs: $taskLogs"
}
