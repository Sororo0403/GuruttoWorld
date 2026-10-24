$ErrorActionPreference = 'Stop'

function Enter-ScriptBuildSession {
    param(
        [Parameter(Mandatory = $true)][string]$OutputContent,
        [Parameter(Mandatory = $true)][string]$Configuration,
        [int]$TimeoutSeconds = 600
    )

    # 同じ出力先へのビルド全体を直列化し、古い入力の後着公開を防ぎます。
    $identity = [IO.Path]::GetFullPath($OutputContent).TrimEnd('\', '/').ToUpperInvariant() + '|' + $Configuration
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = -join ($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($identity)) | ForEach-Object { $_.ToString('x2') })
    } finally {
        $hasher.Dispose()
    }
    $mutex = [Threading.Mutex]::new($false, ('Local\WP1.ScriptBuild.' + $hash))
    $acquired = $false
    try {
        try {
            $acquired = $mutex.WaitOne([TimeSpan]::FromSeconds($TimeoutSeconds))
        } catch [Threading.AbandonedMutexException] {
            $acquired = $true
        }
        if (!$acquired) { throw 'Timed out waiting for another script build for this output.' }
        return $mutex
    } catch {
        $mutex.Dispose()
        throw
    }
}

function Exit-ScriptBuildSession {
    param([Threading.Mutex]$Session)

    if ($null -ne $Session) {
        try { $Session.ReleaseMutex() } finally { $Session.Dispose() }
    }
}

function Publish-ScriptManifest {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Json
    )

    # 成果物を設置してから呼び、公開済み manifest は原子的な交換でだけ更新します。
    $temporary = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Json)
        $stream = [IO.FileStream]::new($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush($true)
        } finally {
            $stream.Dispose()
        }
        if ([IO.File]::Exists($Path)) {
            [IO.File]::Replace($temporary, $Path, [NullString]::Value)
        } else {
            [IO.File]::Move($temporary, $Path)
        }
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}
