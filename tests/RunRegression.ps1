param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [ValidateRange(1,3600)][int]$TimeoutSeconds=1800,
    [string]$Executable
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
if(!$Executable) {$Executable=Join-Path $taskRoot "generated/tests/outputs/x64/$Configuration/ReviewRegressionValidation/ReviewRegressionValidation.exe"}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$taskFolder=Join-Path $taskRoot ("generated/tests/regression-runs/$Configuration-"+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $taskFolder 'temp') -Force | Out-Null
$taskPreviousModes=@(Get-ChildItem Env:WP1_*_ONLY)
$taskSetupMode=Get-Item Env:WP1_SCRIPT_MODULE_SETUP -ErrorAction SilentlyContinue
if($taskSetupMode) {$taskPreviousModes+=$taskSetupMode}
$taskPreviousTemp=$env:TEMP
$taskPreviousTmp=$env:TMP
$taskProcess=$null
try {
    foreach($taskMode in $taskPreviousModes) {Remove-Item ('Env:'+$taskMode.Name)}
    $env:TEMP=Join-Path $taskFolder 'temp';$env:TMP=$env:TEMP
    if(-not ('Wp1RegressionProcess' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Wp1RegressionProcess {
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetExitCodeProcess(IntPtr process, out uint code);
}
'@
    }
    $taskOut=Join-Path $taskFolder 'output.log';$taskErr=Join-Path $taskFolder 'error.log'
    $taskProcess=Start-Process -FilePath $Executable -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $taskOut -RedirectStandardError $taskErr
    # 長時間の実行後も Windows PowerShell で ExitCode を取得できるよう、監視前にプロセスのハンドルを保持します。
    $taskHandle=$taskProcess.Handle
    $taskClock=[Diagnostics.Stopwatch]::StartNew();$taskLast='';$taskLastReport=0
    Write-Output "Regression logs: $taskFolder"
    while($true) {
        $taskRemaining=$TimeoutSeconds*1000-$taskClock.Elapsed.TotalMilliseconds
        $taskWait=[int][Math]::Max(0,[Math]::Min(5000,[Math]::Ceiling($taskRemaining)))
        if($taskProcess.WaitForExit($taskWait)) {break}
        $taskPhase=Get-Content -LiteralPath $taskErr -Tail 1
        if(($taskPhase -and $taskPhase -ne $taskLast) -or $taskClock.Elapsed.TotalSeconds-$taskLastReport -ge 30) {
            if($taskPhase) {$taskLast=$taskPhase}
            Write-Output ("{0:n1}s {1}" -f $taskClock.Elapsed.TotalSeconds,$taskLast)
            $taskLastReport=$taskClock.Elapsed.TotalSeconds
        }
        if($taskClock.Elapsed.TotalSeconds -ge $TimeoutSeconds) {throw "Regression exceeded ${TimeoutSeconds}s. Last phase: $taskLast. Logs: $taskFolder"}
    }
    $taskProcess.WaitForExit()
    Get-Content -LiteralPath $taskOut -Tail 4
    [uint32]$taskExitCode=0
    if(![Wp1RegressionProcess]::GetExitCodeProcess($taskHandle,[ref]$taskExitCode)) {throw "Cannot read regression exit code. Logs: $taskFolder"}
    if($taskExitCode -ne 0) {Get-Content -LiteralPath $taskErr -Tail 20;throw "Regression failed ($taskExitCode). Logs: $taskFolder"}
    Write-Output ("PASS: {0} complete regression in {1:n1}s" -f $Configuration,$taskClock.Elapsed.TotalSeconds)
} finally {
    if($taskProcess) {if(!$taskProcess.HasExited) {$taskProcess.Kill();$taskProcess.WaitForExit()};$taskProcess.Dispose()}
    $env:TEMP=$taskPreviousTemp;$env:TMP=$taskPreviousTmp
    foreach($taskMode in $taskPreviousModes) {Set-Item ('Env:'+$taskMode.Name) $taskMode.Value}
}
