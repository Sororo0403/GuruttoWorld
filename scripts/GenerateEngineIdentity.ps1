param([ValidateSet('Debug', 'Development', 'Release')][string]$Configuration)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'FileHash.ps1')
. (Join-Path $PSScriptRoot 'EngineBuildInputs.ps1')
$identityRoot = Split-Path $PSScriptRoot -Parent
$identityFiles = @(Get-EngineBuildInputFiles -Root $identityRoot)
$identityRecords = @("WP1 engine identity v1 $Configuration")
foreach ($identityFile in $identityFiles | Sort-Object FullName -Unique) {
    $identityRelative = $identityFile.FullName.Substring($identityRoot.Length).Replace('\', '/')
    $identityRecords += $identityRelative + ':' + (Get-Wp1FileHash -LiteralPath $identityFile.FullName)
}
$identityHasher = [Security.Cryptography.SHA256]::Create()
try {
    $identityBytes = [Text.Encoding]::UTF8.GetBytes(($identityRecords -join "`n"))
    $identityHash = -join ($identityHasher.ComputeHash($identityBytes) | ForEach-Object { $_.ToString('x2') })
} finally {
    $identityHasher.Dispose()
}
$identityDirectory = Join-Path $identityRoot "generated/build-identity/$Configuration"
New-Item -ItemType Directory -Path $identityDirectory -Force | Out-Null
$identityPath = Join-Path $identityDirectory 'EngineBuildIdentity.generated.h'
$identitySource = "#pragma once`r`n#include <cstdint>`r`nnamespace Engine { inline constexpr std::uint64_t EngineBuildFingerprint = 0x$($identityHash.Substring(0, 16))ULL; inline constexpr char8_t EngineBuildSourceRoot[] = u8R`"wp1($identityRoot)wp1`"; }`r`n"
if ((Test-Path -LiteralPath $identityPath) -and [IO.File]::ReadAllText($identityPath) -ceq $identitySource) { return }
$identityTemporary = $identityPath + '.' + [Guid]::NewGuid().ToString('N')
try {
    [IO.File]::WriteAllText($identityTemporary, $identitySource, [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $identityPath) { [IO.File]::Replace($identityTemporary, $identityPath, [NullString]::Value) }
    else { [IO.File]::Move($identityTemporary, $identityPath) }
} finally {
    if (Test-Path -LiteralPath $identityTemporary) { Remove-Item -LiteralPath $identityTemporary }
}
