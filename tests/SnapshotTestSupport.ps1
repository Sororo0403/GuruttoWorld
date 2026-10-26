function Assert-SnapshotConcurrentChange {
    param(
        [string]$Content,
        [string]$Destination,
        [string]$MutatedFile,
        [string]$TriggerRelative
    )

    # 最初の対象のコピー後にも処理が続くよう、大きな素材を最後に配置します。
    $padding = Join-Path $Content 'Shaders/ZSnapshotPadding.bin'
    New-Item -ItemType Directory -Path (Split-Path $padding -Parent) -Force | Out-Null
    $stream = [IO.File]::Create($padding)
    try { $stream.SetLength(32MB) } finally { $stream.Dispose() }
    $ready = $Destination + '.writer-ready'
    $trigger = Join-Path $Destination $TriggerRelative
    $writer = Start-Job -ArgumentList $MutatedFile, $trigger, $ready -ScriptBlock {
        param($Source, $Trigger, $Ready)
        [IO.File]::WriteAllText($Ready, 'ready')
        $clock = [Diagnostics.Stopwatch]::StartNew()
        while (!(Test-Path -LiteralPath $Trigger)) {
            if ($clock.Elapsed.TotalSeconds -ge 30) { throw 'Snapshot did not copy the trigger file.' }
            Start-Sleep -Milliseconds 1
        }
        [IO.File]::WriteAllText($Source, 'concurrent edit ' + [Guid]::NewGuid().ToString('N'))
        'changed'
    }
    try {
        $clock = [Diagnostics.Stopwatch]::StartNew()
        while (!(Test-Path -LiteralPath $ready)) {
            if ($clock.Elapsed.TotalSeconds -ge 15) { throw 'Snapshot writer did not start.' }
            Start-Sleep -Milliseconds 10
        }
        $rejected = $false
        try { Copy-BuildContentSnapshot -Content $Content -Destination $Destination } catch {
            $rejected = $_.Exception.Message -like '*Content changed*'
        }
        $change = @(Receive-Job -Job $writer -Wait -ErrorAction Stop)
        if ($change -notcontains 'changed' -or !$rejected) {
            throw 'Snapshot did not reject an edit after copying the source file.'
        }
    } finally {
        Stop-Job -Job $writer -ErrorAction SilentlyContinue
        Remove-Job -Job $writer -Force
        Remove-Item -LiteralPath $padding, $ready -ErrorAction SilentlyContinue
    }
}
