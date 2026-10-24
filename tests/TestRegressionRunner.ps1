param()
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskFolder=Join-Path $taskRoot ('generated/tests/runner-selftests/'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFolder -Force | Out-Null
$taskSource=Join-Path $taskFolder 'RunnerFixture.cs'
$taskExecutable=Join-Path $taskFolder 'RunnerFixture.exe'
@'
using System;
using System.IO;
using System.Threading;
using System.Diagnostics;
class RunnerFixture {
    static int Main(string[] args) {
        var childPid = Environment.GetEnvironmentVariable("WP1_RUNNER_TEST_CHILD_PID");
        if (args.Length == 1 && args[0] == "--child") {
            File.WriteAllText(childPid, Process.GetCurrentProcess().Id.ToString());
            Thread.Sleep(30000);
            return 0;
        }
        File.WriteAllText(Environment.GetEnvironmentVariable("WP1_RUNNER_TEST_PID"), Process.GetCurrentProcess().Id.ToString());
        if (!string.IsNullOrEmpty(childPid)) {
            var child = new ProcessStartInfo(Process.GetCurrentProcess().MainModule.FileName, "--child");
            child.UseShellExecute = false;
            child.CreateNoWindow = true;
            using (Process.Start(child)) {
                var deadline = Stopwatch.StartNew();
                while (!File.Exists(childPid) && deadline.ElapsedMilliseconds < 5000) Thread.Sleep(10);
            }
        }
        var counter = Environment.GetEnvironmentVariable("WP1_RUNNER_TEST_COUNTER");
        if (!string.IsNullOrEmpty(counter)) {
            File.AppendAllText(counter, "run\n");
            var failAt = Environment.GetEnvironmentVariable("WP1_RUNNER_TEST_FAIL_AT");
            if (!string.IsNullOrEmpty(failAt) && File.ReadAllLines(counter).Length == int.Parse(failAt)) return 7;
        }
        Thread.Sleep(int.Parse(Environment.GetEnvironmentVariable("WP1_RUNNER_TEST_DELAY")));
        if (Environment.GetEnvironmentVariable("WP1_RUNNER_TEST_NOISY") == "1") {
            for (int i = 0; i < 60; ++i) Console.Error.WriteLine("Validation phase: fixture " + i);
            Console.Error.WriteLine("ROOT CAUSE fixture error");
            for (int i = 0; i < 60; ++i) Console.Error.WriteLine("fixture tail " + i);
        }
        Console.WriteLine("selectors=" + Environment.GetEnvironmentVariable("WP1_INPUT_ONLY") + "|" + Environment.GetEnvironmentVariable("WP1_SCRIPT_MODULE_SETUP"));
        return int.Parse(Environment.GetEnvironmentVariable("WP1_RUNNER_TEST_EXIT"));
    }
}
'@ | Set-Content -LiteralPath $taskSource -Encoding UTF8
& "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe" /nologo /target:exe "/out:$taskExecutable" $taskSource
if($LASTEXITCODE) {throw 'Runner fixture compilation failed'}
$taskNames=@('WP1_RUNNER_TEST_PID','WP1_RUNNER_TEST_DELAY','WP1_RUNNER_TEST_EXIT','WP1_INPUT_ONLY','WP1_SCRIPT_MODULE_SETUP','WP1_RUNNER_TEST_COUNTER','WP1_RUNNER_TEST_FAIL_AT','WP1_RUNNER_TEST_NOISY','WP1_RUNNER_TEST_CHILD_PID')
$taskSaved=@{}
foreach($taskName in $taskNames) {$taskSaved[$taskName]=[Environment]::GetEnvironmentVariable($taskName,'Process')}
try {
    $env:WP1_INPUT_ONLY='runner-selftest';$env:WP1_SCRIPT_MODULE_SETUP='runner-selftest'
    $env:WP1_RUNNER_TEST_DELAY='250';$env:WP1_RUNNER_TEST_EXIT='0'
    $env:WP1_RUNNER_TEST_PID=Join-Path $taskFolder 'success.pid'
    $taskOutput=& (Join-Path $PSScriptRoot 'RunRegression.ps1') -Executable $taskExecutable -TimeoutSeconds 10
    if($taskOutput -notcontains 'selectors=|') {throw 'Runner inherited focused test selectors'}
    foreach($taskFailure in @('exit','timeout')) {
        $env:WP1_RUNNER_TEST_PID=Join-Path $taskFolder ($taskFailure+'.pid')
        $env:WP1_RUNNER_TEST_DELAY=if($taskFailure -eq 'timeout') {'1500'} else {'250'}
        $env:WP1_RUNNER_TEST_EXIT=if($taskFailure -eq 'exit') {'7'} else {'0'}
        $taskTimeout=if($taskFailure -eq 'timeout') {1} else {10}
        $taskExpected=if($taskFailure -eq 'timeout') {'Regression exceeded 1s.'} else {'Regression failed (7).'}
        $env:WP1_RUNNER_TEST_CHILD_PID = if ($taskFailure -eq 'timeout') { Join-Path $taskFolder 'timeout-child.pid' } else { $null }
        $taskRejected=$false
        try {& (Join-Path $PSScriptRoot 'RunRegression.ps1') -Executable $taskExecutable -TimeoutSeconds $taskTimeout | Out-Null}
        catch {if(!$_.Exception.Message.Contains($taskExpected)) {throw};$taskRejected=$true}
        if(!$taskRejected) {throw "Runner accepted $taskFailure failure"}
        if($taskFailure -eq 'timeout') {
            $taskChildId=[int](Get-Content -LiteralPath $env:WP1_RUNNER_TEST_PID)
            if(Get-Process -Id $taskChildId -ErrorAction SilentlyContinue) {throw 'Timed-out fixture is still running'}
            $taskDescendantId = [int](Get-Content -LiteralPath $env:WP1_RUNNER_TEST_CHILD_PID)
            $taskDescendant = Get-Process -Id $taskDescendantId -ErrorAction SilentlyContinue
            if ($taskDescendant) {
                # 失敗前の実装でも、検証自身が生成した子プロセスを残さないようにします。
                $taskDescendant.Kill()
                $taskDescendant.WaitForExit()
                $taskDescendant.Dispose()
                throw 'Timed-out fixture descendant is still running'
            }
        }
    }
    $env:WP1_RUNNER_TEST_CHILD_PID = $null
    $env:WP1_RUNNER_TEST_NOISY = '1'
    $env:WP1_RUNNER_TEST_DELAY = '50'
    $env:WP1_RUNNER_TEST_EXIT = '7'
    $env:WP1_RUNNER_TEST_PID = Join-Path $taskFolder 'noisy-failure.pid'
    $taskDiagnostics = [Collections.Generic.List[string]]::new()
    $taskRejected = $false
    try { & (Join-Path $PSScriptRoot 'RunRegression.ps1') -Executable $taskExecutable -TimeoutSeconds 10 | ForEach-Object { $taskDiagnostics.Add([string]$_) } }
    catch { if (!$_.Exception.Message.Contains('Regression failed (7).')) { throw }; $taskRejected = $true }
    if (!$taskRejected -or !$taskDiagnostics.Contains('ROOT CAUSE fixture error') -or !$taskDiagnostics.Contains('fixture tail 59')) { throw 'Runner hid the root cause or final diagnostics.' }
    $env:WP1_RUNNER_TEST_NOISY = $null
    if($env:WP1_INPUT_ONLY -ne 'runner-selftest' -or $env:WP1_SCRIPT_MODULE_SETUP -ne 'runner-selftest') {throw 'Runner did not restore focused test selectors'}
    $env:WP1_RUNNER_TEST_DELAY = '50'
    $env:WP1_RUNNER_TEST_EXIT = '0'
    $env:WP1_RUNNER_TEST_COUNTER = Join-Path $taskFolder 'repeated-success.txt'
    $taskCulture = [Threading.Thread]::CurrentThread.CurrentCulture
    try {
        $taskProbeCulture = [Globalization.CultureInfo]::GetCultureInfo('ar-SA').Clone()
        $taskProbeCulture.NumberFormat.NumberDecimalSeparator = [string][char]0x066b
        $taskProbeCulture.NumberFormat.NumberGroupSeparator = [string][char]0x066c
        [Threading.Thread]::CurrentThread.CurrentCulture = $taskProbeCulture
        $taskOutput = & (Join-Path $PSScriptRoot 'RunRegression.ps1') -Executable $taskExecutable -TimeoutSeconds 10 -Runs 3
    } finally {
        [Threading.Thread]::CurrentThread.CurrentCulture = $taskCulture
    }
    if (@(Get-Content -LiteralPath $env:WP1_RUNNER_TEST_COUNTER).Count -ne 3) { throw 'Repeated runner did not execute all three processes.' }
    $taskSummaryLine = @($taskOutput | Where-Object { $_ -like 'Repeated regression summary:*' })
    if ($taskSummaryLine.Count -ne 1) { throw 'Repeated runner did not publish one summary.' }
    $taskSummary = Get-Content -LiteralPath ($taskSummaryLine[0] -replace '^Repeated regression summary: ','') -Raw | ConvertFrom-Json
    if ($taskSummary.runs -ne 3 -or $taskSummary.seconds.Count -ne 3) { throw 'Repeated runner summary has an incorrect sample count.' }
    $env:WP1_RUNNER_TEST_COUNTER = Join-Path $taskFolder 'repeated-failure.txt'
    $env:WP1_RUNNER_TEST_FAIL_AT = '2'
    $taskRejected = $false
    try { & (Join-Path $PSScriptRoot 'RunRegression.ps1') -Executable $taskExecutable -TimeoutSeconds 10 -Runs 3 | Out-Null }
    catch { if (!$_.Exception.Message.Contains('Regression failed (7).')) { throw }; $taskRejected = $true }
    if (!$taskRejected -or @(Get-Content -LiteralPath $env:WP1_RUNNER_TEST_COUNTER).Count -ne 2) { throw 'Repeated runner continued after a failed process.' }
    Write-Output 'PASS: runner success, exit 7, diagnostic head/tail, selector isolation/restoration, exact deadline, process-tree cleanup, repeated runs, localized numbers and fail-fast'
} finally {
    foreach($taskName in $taskNames) {[Environment]::SetEnvironmentVariable($taskName,$taskSaved[$taskName],'Process')}
}
