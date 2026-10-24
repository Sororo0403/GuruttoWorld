param(
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [ValidateRange(1,3600)][int]$TimeoutSeconds=1800,
    [string]$Executable,
    [ValidateRange(1,20)][int]$Runs=1
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
if(!$Executable) {$Executable=Join-Path $taskRoot "generated/tests/outputs/x64/$Configuration/ReviewRegressionValidation/ReviewRegressionValidation.exe"}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
if ($Runs -gt 1) {
    $taskRunTimes = [Collections.Generic.List[double]]::new()
    for ($taskRun = 1; $taskRun -le $Runs; ++$taskRun) {
        Write-Output "Regression run $taskRun / $Runs"
        & $PSCommandPath -Configuration $Configuration -TimeoutSeconds $TimeoutSeconds -Executable $Executable -Runs 1 |
            ForEach-Object {
                Write-Output $_
                if ($_ -match '^PASS: .+ complete regression in (.+)s$') {
                    $taskRunTimes.Add([double]::Parse($Matches[1], [Globalization.CultureInfo]::CurrentCulture))
                }
            }
    }
    if ($taskRunTimes.Count -ne $Runs) { throw 'Not all repeated regression runs completed.' }
    $taskSorted = @($taskRunTimes | Sort-Object)
    $taskMiddle = [int][Math]::Floor($Runs / 2)
    $taskMedian = $taskSorted[$taskMiddle]
    if ($Runs % 2 -eq 0) { $taskMedian = ($taskSorted[$taskMiddle - 1] + $taskMedian) / 2 }
    $taskSummary = [ordered]@{
        configuration = $Configuration
        executable = $Executable
        runs = $Runs
        seconds = @($taskRunTimes)
        minimumSeconds = $taskSorted[0]
        medianSeconds = $taskMedian
        maximumSeconds = $taskSorted[-1]
        buildTimeIncluded = $false
    }
    $taskSummaryPath = Join-Path $taskRoot ('generated/tests/regression-runs/repeated-' + [Guid]::NewGuid().ToString('N') + '.json')
    $taskSummary | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $taskSummaryPath -Encoding UTF8
    Write-Output "Repeated regression summary: $taskSummaryPath"
    Write-Output ("PASS: {0} full runs; min {1:n1}s, median {2:n1}s, max {3:n1}s" -f $Runs,$taskSorted[0],$taskMedian,$taskSorted[-1])
    return
}
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
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
public static class Wp1RegressionProcess {
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetExitCodeProcess(IntPtr process, out uint code);
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    struct ProcessEntry {
        public uint size, usage, processId;
        public UIntPtr defaultHeap;
        public uint moduleId, threads, parentId;
        public int priority;
        public uint flags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string executable;
    }
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint processId);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool Process32FirstW(IntPtr snapshot, ref ProcessEntry entry);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool Process32NextW(IntPtr snapshot, ref ProcessEntry entry);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    public static void StopDescendants(Process root) {
        var capturedAt = DateTime.UtcNow;
        var rootStartedAt = root.StartTime.ToUniversalTime();
        var snapshot = CreateToolhelp32Snapshot(2, 0);
        if (snapshot == new IntPtr(-1)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        var parents = new Dictionary<int, int>();
        try {
            var entry = new ProcessEntry();
            entry.size = (uint)Marshal.SizeOf(typeof(ProcessEntry));
            if (!Process32FirstW(snapshot, ref entry)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            do { parents[(int)entry.processId] = (int)entry.parentId; } while (Process32NextW(snapshot, ref entry));
        } finally { CloseHandle(snapshot); }
        var children = new List<int>();
        var pending = new Queue<int>();
        var visited = new HashSet<int>();
        pending.Enqueue(root.Id);
        visited.Add(root.Id);
        while (pending.Count != 0) {
            var parent = pending.Dequeue();
            foreach (var entry in parents) if (entry.Value == parent && visited.Add(entry.Key)) {
                children.Add(entry.Key);
                pending.Enqueue(entry.Key);
            }
        }
        children.Reverse();
        foreach (var childId in children) {
            try {
                using (var child = Process.GetProcessById(childId)) {
                    var handle = child.Handle;
                    var startedAt = child.StartTime.ToUniversalTime();
                    // スナップショット後に同じPIDへ入れ替わったプロセスを終了しないようにします。
                    if (startedAt >= rootStartedAt && startedAt <= capturedAt && !child.HasExited) {
                        child.Kill();
                        if (!child.WaitForExit(10000)) throw new TimeoutException("Regression descendant did not stop");
                    }
                }
            } catch (ArgumentException) {
                // 既に終了した子プロセスは停止済みとして扱います。
            } catch (InvalidOperationException) {
                // ハンドル取得前に終了した子プロセスも同様に扱います。
            }
        }
    }
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
            $taskProcess.Refresh()
            Write-Output ("{0:n1}s [CPU {1:n1}s] {2}" -f $taskClock.Elapsed.TotalSeconds,$taskProcess.TotalProcessorTime.TotalSeconds,$taskLast)
            $taskLastReport=$taskClock.Elapsed.TotalSeconds
        }
        if($taskClock.Elapsed.TotalSeconds -ge $TimeoutSeconds) {throw "Regression exceeded ${TimeoutSeconds}s. Last phase: $taskLast. Logs: $taskFolder"}
    }
    $taskProcess.WaitForExit()
    Get-Content -LiteralPath $taskOut -Tail 4
    [uint32]$taskExitCode=0
    if(![Wp1RegressionProcess]::GetExitCodeProcess($taskHandle,[ref]$taskExitCode)) {throw "Cannot read regression exit code. Logs: $taskFolder"}
    if($taskExitCode -ne 0) {
        # 終了時に出る大量のログで、先頭に記録された失敗原因を隠さないようにします。
        $taskDiagnosticHead = @(Get-Content -LiteralPath $taskErr |
            Where-Object { $_ -notmatch '^(Validation phase:|Title validation:)' } |
            Select-Object -First 21)
        $taskDiagnosticHead | Select-Object -First 20
        if ($taskDiagnosticHead.Count -gt 20) { Get-Content -LiteralPath $taskErr -Tail 20 }
        throw "Regression failed ($taskExitCode). Logs: $taskFolder"
    }
    Write-Output ("PASS: {0} complete regression in {1:n1}s" -f $Configuration,$taskClock.Elapsed.TotalSeconds)
} finally {
    try {
        if ($taskProcess) {
            try {
                if (!$taskProcess.HasExited) {
                    # Windows PowerShell の Kill() は親だけを終了するため、先に生成された子孫も停止します。
                    try {
                        [Wp1RegressionProcess]::StopDescendants($taskProcess)
                    } catch {
                        # 子孫の列挙に失敗した場合も、監視対象の親プロセスは残さないようにします。
                        Write-Warning ('Cannot stop regression process tree: ' + $_.Exception.Message)
                    }
                    if (!$taskProcess.HasExited) {
                        $taskProcess.Kill()
                    }
                    $taskProcess.WaitForExit()
                }
            } finally {
                $taskProcess.Dispose()
            }
        }
    } finally {
        $env:TEMP = $taskPreviousTemp
        $env:TMP = $taskPreviousTmp
        foreach ($taskMode in $taskPreviousModes) {
            Set-Item ('Env:' + $taskMode.Name) $taskMode.Value
        }
    }
}
