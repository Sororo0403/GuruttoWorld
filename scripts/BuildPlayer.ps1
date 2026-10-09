param(
    [ValidateSet('Release','Development')][string]$Configuration='Release',
    [ValidatePattern('^[a-f0-9]{32}$')][string]$RunId=([Guid]::NewGuid().ToString('N')),
    [switch]$SkipBuild
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$buildRoot=Join-Path $repoRoot 'generated/builds'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$resultPath=Join-Path $buildRoot "$RunId.json"
$logPath=Join-Path $buildRoot "$RunId.log"
Start-Transcript -Path $logPath -Force | Out-Null
function Set-BuildProgress {
    param([int]$Step,[string]$Phase)
    $progressPath=Join-Path $buildRoot "$RunId.progress.json"
    [ordered]@{step=$Step;phase=$Phase} | ConvertTo-Json | Set-Content -LiteralPath "$progressPath.tmp" -Encoding UTF8
    Move-Item -LiteralPath "$progressPath.tmp" -Destination $progressPath -Force
}
try {
    $package=Join-Path $buildRoot "$Configuration-$RunId"
    if (Test-Path -LiteralPath $package) { throw 'Package destination already exists.' }
    $staging="$package.building"
    if (Test-Path -LiteralPath $staging) { throw 'Staging destination already exists.' }
    New-Item -ItemType Directory -Path $staging | Out-Null
    Set-BuildProgress 0 '保存済みContentのスナップショットを作成中'
    . (Join-Path $PSScriptRoot 'BuildContentSnapshot.ps1')
    Copy-BuildContentSnapshot -Content (Join-Path $repoRoot 'Content') -Destination $staging
    Set-BuildProgress 1 'Appをコンパイル中'
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere.exe is required.' }
    $installation=& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
    if (!$installation) { throw 'Visual Studio with MSBuild is required.' }
    $msbuild=Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
    if (!$SkipBuild) {
        & $msbuild (Join-Path $repoRoot 'App/App.vcxproj') "/p:Configuration=$Configuration" '/p:Platform=x64' '/m' '/v:minimal' '/nologo'
        if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($LASTEXITCODE)." }
    }
    $executable=Join-Path $repoRoot "generated/outputs/x64/$Configuration/App/App.exe"
    if (!(Test-Path -LiteralPath $executable)) { throw 'Built App.exe is missing.' }
    Set-BuildProgress 2 '配布用C++ゲーム処理をコンパイル中'
    $scriptShell=Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'
    & $scriptShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'BuildScripts.ps1') -Configuration $Configuration -Content $staging -OutputContent $staging -RunId "$RunId-scripts"
    $scriptResult=Get-Content -LiteralPath (Join-Path $repoRoot "generated/script-builds/$RunId-scripts.json") -Raw | ConvertFrom-Json
    if (!$scriptResult.success) { throw $scriptResult.error }
    Set-BuildProgress 2 '実行ファイル・ランタイムをパッケージ中'
    Copy-Item -LiteralPath $executable -Destination (Join-Path $staging 'App.exe')
    $licenseFolder=Join-Path (Split-Path $executable -Parent) 'Licenses'
    if (Test-Path -LiteralPath $licenseFolder) { Copy-Item -LiteralPath $licenseFolder -Destination (Join-Path $staging 'Licenses') -Recurse }
    $redistRoot=Join-Path $installation 'VC/Redist/MSVC'
    $version=Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (!$version) { throw 'MSVC redistributable runtime is missing.' }
    $crt=Get-ChildItem -LiteralPath (Join-Path $version.FullName 'x64') -Directory | Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' } | Select-Object -First 1
    if (!$crt) { throw 'x64 MSVC CRT is missing.' }
    Get-ChildItem -LiteralPath $crt.FullName -File -Filter '*.dll' | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $staging }
    $project=Get-Content -LiteralPath (Join-Path $staging 'Assets/Project.json') -Raw | ConvertFrom-Json
    $startup=$project.startupScene
    if (!(Test-Path -LiteralPath (Join-Path $staging $startup))) {
        $referenceId=$null
        if ($project.assetReferences) { $referenceId=$project.assetReferences.PSObject.Properties[$startup].Value }
        foreach ($metaFile in Get-ChildItem -LiteralPath (Join-Path $staging 'Assets') -File -Recurse -Filter '*.meta') {
            $metadata=Get-Content -LiteralPath $metaFile.FullName -Raw | ConvertFrom-Json
            if (($referenceId -and $metadata.id -eq $referenceId) -or (!$referenceId -and $metadata.previousPaths -contains $startup)) {
                $startup=$metaFile.FullName.Substring($staging.Length+1).Replace('\','/'); $startup=$startup.Substring(0,$startup.Length-5); break
            }
        }
    }
    if (!(Test-Path -LiteralPath (Join-Path $staging $startup))) { throw 'Packaged startup scene is missing.' }
    $files=@(Get-ChildItem -LiteralPath $staging -File -Recurse | ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($staging.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-Wp1FileHash -LiteralPath $_.FullName).ToLowerInvariant()}
    })
    [ordered]@{configuration=$Configuration;createdUtc=[DateTime]::UtcNow.ToString('o');startupScene=$startup;files=$files} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $staging 'package.json') -Encoding UTF8
    Set-BuildProgress 3 'パッケージの起動を検証中'
    $validation=Start-Process -FilePath (Join-Path $staging 'App.exe') -ArgumentList '--validate-package' -WorkingDirectory $staging -WindowStyle Hidden -PassThru
    $validationStarted=[DateTime]::UtcNow
    while (!$validation.WaitForExit(5000)) {
        Write-Output 'Waiting for packaged startup validation...'
        if (([DateTime]::UtcNow-$validationStarted).TotalSeconds -ge 180) { $validation.Kill(); throw 'Package startup validation timed out.' }
    }
    if ($validation.ExitCode -ne 0) { throw "Package startup validation failed ($($validation.ExitCode)); see Diagnostics/package-validation.log." }
    Move-Item -LiteralPath $staging -Destination $package
    Set-BuildProgress 4 '完了'
    [ordered]@{success=$true;package=$package;log=$logPath;configuration=$Configuration} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
    Write-Output "Package complete: $package"
} catch {
    [ordered]@{success=$false;error=$_.Exception.Message;log=$logPath;configuration=$Configuration} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
    Write-Output $_.Exception.Message
    exit 1
} finally { Stop-Transcript | Out-Null }
