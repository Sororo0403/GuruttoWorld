function Get-Wp1FileHash {
    param([Parameter(Mandatory=$true)][string]$LiteralPath)
    $taskAlgorithm=[Security.Cryptography.SHA256]::Create()
    $taskStream=$null
    try {
        $taskStream=[IO.File]::OpenRead($LiteralPath)
        return [BitConverter]::ToString($taskAlgorithm.ComputeHash($taskStream)).Replace('-','')
    }
    finally {
        if ($taskStream) { $taskStream.Dispose() }
        $taskAlgorithm.Dispose()
    }
}
