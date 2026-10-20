param(
    [Parameter(Mandatory = $true)][string]$Content,
    [ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Development'
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskContent = [IO.Path]::GetFullPath($Content)
$taskSource = Join-Path $taskContent 'Assets/Scripts'
if (!(Test-Path -LiteralPath $taskSource)) { return }
$taskDeclarations = @(Get-ChildItem -LiteralPath $taskSource -File -Recurse | Where-Object { $_.Name.EndsWith('.schema.json', [StringComparison]::Ordinal) })
if ($taskDeclarations.Count -eq 0) { return }

$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskInstallation = & $taskVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (!$taskInstallation) { throw 'Visual Studio C++ is required to generate script schemas.' }
$taskProject = Join-Path $taskRoot 'Tools/ScriptSchemaCompiler/ScriptSchemaCompiler.vcxproj'
& (Join-Path $taskInstallation 'MSBuild/Current/Bin/MSBuild.exe') $taskProject "/p:Configuration=$Configuration" /p:Platform=x64 /p:PreferredToolArchitecture=x64 /m:1 /nr:false /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Schema compiler build failed.' }
$taskCompiler = Join-Path $taskRoot "generated/outputs/x64/$Configuration/ScriptSchemaCompiler/ScriptSchemaCompiler.exe"
& $taskCompiler $taskContent
if ($LASTEXITCODE -ne 0) { throw 'Script schema generation failed; declarations were not accepted.' }
