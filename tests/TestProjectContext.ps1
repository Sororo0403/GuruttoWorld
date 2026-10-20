param()

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $taskRoot 'scripts/ProjectContext.ps1')
. (Join-Path $taskRoot 'scripts/FileHash.ps1')
$taskFolder = Join-Path $taskRoot ('generated/tests/project-context-' + [Guid]::NewGuid().ToString('N'))
$taskGame = Join-Path $taskFolder 'Original Game'
& (Join-Path $taskRoot 'scripts/NewGameProject.ps1') -Destination $taskGame -Name Minimal
$taskContext = Get-ProjectContext -Project $taskGame -RequireEngineRoot $taskRoot
$taskIdentity = $taskContext.projectId
if (!$taskContext.generatedProjects -or $taskIdentity -cnotmatch '^[a-f0-9]{32}$') { throw 'Generated project has no stable identity.' }
$taskScene = Get-Content -LiteralPath (Join-Path $taskGame 'Assets/Scenes/Main.json') -Raw | ConvertFrom-Json
if ($taskScene.objects.Count -ne 0 -or (Test-Path -LiteralPath (Join-Path $taskGame 'Assets/Scripts'))) { throw 'Minimal project contains game-specific content.' }
$taskMetadata = Get-Content -LiteralPath (Join-Path $taskGame 'Assets/Project.json.meta') -Raw | ConvertFrom-Json
if ($taskMetadata.id -cne $taskIdentity) { throw 'New project identity and legacy settings identity differ.' }
$taskProjectHash = Get-Wp1FileHash -LiteralPath (Join-Path $taskGame 'Minimal.vcxproj')
$taskSolutionHash = Get-Wp1FileHash -LiteralPath (Join-Path $taskGame 'Minimal.slnx')
Sync-GeneratedGameProject -Context $taskContext
if ((Get-Wp1FileHash -LiteralPath (Join-Path $taskGame 'Minimal.vcxproj')) -ne $taskProjectHash -or
    (Get-Wp1FileHash -LiteralPath (Join-Path $taskGame 'Minimal.slnx')) -ne $taskSolutionHash) { throw 'Regeneration is not deterministic.' }
[IO.File]::WriteAllText((Join-Path $taskGame 'Minimal.vcxproj'), '<Project />')
Sync-GeneratedGameProject -Context $taskContext
if ((Get-Wp1FileHash -LiteralPath (Join-Path $taskGame 'Minimal.vcxproj')) -ne $taskProjectHash) { throw 'Generated file replacement failed.' }

$taskMoved = [IO.Path]::GetFullPath((Join-Path $taskFolder 'Moved Game'))
$taskSource = [IO.Path]::GetFullPath($taskGame)
$taskBoundary = [IO.Path]::GetFullPath($taskFolder) + [IO.Path]::DirectorySeparatorChar
if (!$taskSource.StartsWith($taskBoundary) -or !$taskMoved.StartsWith($taskBoundary)) { throw 'Project relocation leaves test folder.' }
Move-Item -LiteralPath $taskSource -Destination $taskMoved
$taskContext = Get-ProjectContext -Project $taskMoved -RequireEngineRoot $taskRoot
Sync-GeneratedGameProject -Context $taskContext
if ($taskContext.projectId -cne $taskIdentity) { throw 'Relocation changed project identity.' }
[xml]$taskProjectXml = Get-Content -LiteralPath (Join-Path $taskMoved 'Minimal.vcxproj') -Raw
if ($taskProjectXml.Project.PropertyGroup.WP1Root.TrimEnd('\') -cne $taskRoot.TrimEnd('\')) { throw 'Generated project uses a stale engine reference.' }
. (Join-Path $taskRoot 'scripts/BuildContentSnapshot.ps1')
$taskChangedMetadata = Join-Path $taskMoved 'Assets/Project.json.meta'
$taskMetadata.id = '11111111111111111111111111111111'
[IO.File]::WriteAllText($taskChangedMetadata, ($taskMetadata | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
$taskSnapshot = Join-Path $taskFolder 'Snapshot'
Copy-BuildContentSnapshot -Content $taskMoved -Destination $taskSnapshot
$taskRuntime = Get-Content -LiteralPath (Join-Path $taskSnapshot 'WP1.runtime.json') -Raw | ConvertFrom-Json
if ($taskRuntime.projectId -cne $taskIdentity -or (Test-Path -LiteralPath (Join-Path $taskSnapshot 'WP1.project.json'))) {
    throw 'Package did not preserve identity independently of asset metadata or exposed source engine reference.'
}
$taskResnapshot = Join-Path $taskFolder 'Resnapshot'
Copy-BuildContentSnapshot -Content $taskSnapshot -Destination $taskResnapshot
$taskRuntime = Get-Content -LiteralPath (Join-Path $taskResnapshot 'WP1.runtime.json') -Raw | ConvertFrom-Json
if ($taskRuntime.projectId -cne $taskIdentity) { throw 'Runtime project identity was lost during repackaging.' }
$taskSynchronized = Join-Path $taskFolder 'Synchronized'
& (Join-Path $taskRoot 'scripts/SyncBuildContent.ps1') -Content $taskMoved -Destination $taskSynchronized
$taskRuntime = Get-Content -LiteralPath (Join-Path $taskSynchronized 'WP1.runtime.json') -Raw | ConvertFrom-Json
if ($taskRuntime.projectId -cne $taskIdentity) { throw 'Player content synchronization lost project identity.' }

foreach ($taskMutation in @('wrong-host', 'bad-version', 'bad-contract', 'bad-name', 'bad-id')) {
    $taskDescriptorPath = Join-Path $taskMoved 'WP1.project.json'
    $taskOriginal = [IO.File]::ReadAllText($taskDescriptorPath)
    $taskDescriptor = $taskOriginal | ConvertFrom-Json
    try {
        switch ($taskMutation) {
            'wrong-host' { $taskDescriptor.engineRoot = $taskFolder }
            'bad-version' { $taskDescriptor.version = 999 }
            'bad-contract' { $taskDescriptor.engineContract = 999 }
            'bad-name' { $taskDescriptor.name = '../Escape' }
            'bad-id' { $taskDescriptor.projectId = '' }
        }
        [IO.File]::WriteAllText($taskDescriptorPath, ($taskDescriptor | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
        $taskRejected = $false
        try { $null = Get-ProjectContext -Project $taskMoved -RequireEngineRoot $taskRoot } catch { $taskRejected = $true }
        if (!$taskRejected) { throw "Invalid descriptor accepted: $taskMutation" }
    } finally {
        [IO.File]::WriteAllText($taskDescriptorPath, $taskOriginal, [Text.UTF8Encoding]::new($false))
    }
}
$taskContext = Get-ProjectContext -Project (Join-Path $taskRoot 'Games/GuruttoWorld') -RequireEngineRoot $taskRoot
if ($taskContext.generatedProjects) { throw 'Legacy hand-maintained project was marked generated.' }
$taskRejected = $false
try { Sync-GeneratedGameProject -Context $taskContext } catch { $taskRejected = $true }
if (!$taskRejected) { throw 'Legacy build files could be overwritten.' }
Write-Output "PASS: minimal template, identity, relocation, deterministic generation, incompatible descriptors and legacy protection: $taskFolder"
