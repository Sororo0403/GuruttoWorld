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
    static int Main() {
        File.WriteAllText(Environment.GetEnvironmentVariable("WP1_RUNNER_TEST_PID"), Process.GetCurrentProcess().Id.ToString());
        Thread.Sleep(int.Parse(Environment.GetEnvironmentVariable("WP1_RUNNER_TEST_DELAY")));
        Console.WriteLine("selectors=" + Environment.GetEnvironmentVariable("WP1_INPUT_ONLY") + "|" + Environment.GetEnvironmentVariable("WP1_SCRIPT_MODULE_SETUP"));
        return int.Parse(Environment.GetEnvironmentVariable("WP1_RUNNER_TEST_EXIT"));
    }
}
'@ | Set-Content -LiteralPath $taskSource -Encoding UTF8
& "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe" /nologo /target:exe "/out:$taskExecutable" $taskSource
if($LASTEXITCODE) {throw 'Runner fixture compilation failed'}
$taskNames=@('WP1_RUNNER_TEST_PID','WP1_RUNNER_TEST_DELAY','WP1_RUNNER_TEST_EXIT','WP1_INPUT_ONLY','WP1_SCRIPT_MODULE_SETUP')
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
        $taskRejected=$false
        try {& (Join-Path $PSScriptRoot 'RunRegression.ps1') -Executable $taskExecutable -TimeoutSeconds $taskTimeout | Out-Null}
        catch {if(!$_.Exception.Message.Contains($taskExpected)) {throw};$taskRejected=$true}
        if(!$taskRejected) {throw "Runner accepted $taskFailure failure"}
        if($taskFailure -eq 'timeout') {
            $taskChildId=[int](Get-Content -LiteralPath $env:WP1_RUNNER_TEST_PID)
            if(Get-Process -Id $taskChildId -ErrorAction SilentlyContinue) {throw 'Timed-out fixture is still running'}
        }
    }
    if($env:WP1_INPUT_ONLY -ne 'runner-selftest' -or $env:WP1_SCRIPT_MODULE_SETUP -ne 'runner-selftest') {throw 'Runner did not restore focused test selectors'}
    Write-Output 'PASS: runner success, exit 7, selector isolation/restoration, exact deadline and child cleanup'
} finally {
    foreach($taskName in $taskNames) {[Environment]::SetEnvironmentVariable($taskName,$taskSaved[$taskName],'Process')}
}
