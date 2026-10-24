param(
    [ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Debug',
    [switch]$BuildNative
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskFixture = Join-Path $taskRoot ('generated/tests/schema compiler 日本語/' + [Guid]::NewGuid().ToString('N'))
$taskScripts = Join-Path $taskFixture 'Assets/Scripts'
New-Item -ItemType Directory -Path $taskScripts -Force | Out-Null
$taskUtf8 = [Text.UTF8Encoding]::new($false)
$taskBase = @{ version = 1; scripts = @{ Base = @{ parameters = @{ speed = @{ initial = 2; minimum = 0; maximum = 10 } }; data = @{}; purposes = @{} } } }
$taskDerived = @{ version = 2; scripts = @{ Derived = @{ extends = 'Base'; parameters = @{ speed = @{ initial = 4; minimum = 1; maximum = 8 } } } } }
[IO.File]::WriteAllText((Join-Path $taskScripts 'Base.schema.json'), ($taskBase | ConvertTo-Json -Depth 12), $taskUtf8)
[IO.File]::WriteAllText((Join-Path $taskScripts 'Derived.schema.json'), ($taskDerived | ConvertTo-Json -Depth 12), $taskUtf8)
$taskLarge = @{ version = 1; scripts = @{ LongSchema = @{ parameters = @{}; data = @{}; purposes = @{} } } }
foreach ($taskIndex in 0..31) { $taskLarge.scripts.LongSchema.data["field$taskIndex"] = @{ kind = 'string'; value = ('語' * 1024) } }
[IO.File]::WriteAllText((Join-Path $taskScripts 'Long.schema.json'), ($taskLarge | ConvertTo-Json -Depth 12), $taskUtf8)
$taskNamesake = @{ version = 1; scripts = @{ Script = @{} } }
[IO.File]::WriteAllText((Join-Path $taskScripts 'Script.schema.json'), ($taskNamesake | ConvertTo-Json -Depth 12), $taskUtf8)
& (Join-Path $taskRoot 'scripts/GenerateScriptSchemas.ps1') -Content $taskFixture -Configuration $Configuration
if (!$?) { throw 'Valid declarations failed generation.' }
$taskCompiler = Join-Path $taskRoot "generated/outputs/x64/$Configuration/ScriptSchemaCompiler/ScriptSchemaCompiler.exe"
$taskHeaders = @(Get-ChildItem -LiteralPath $taskScripts -Filter '*.generated.h')
if ($taskHeaders.Count -ne 4) { throw 'Expected all declaration headers.' }
$taskHashes = @{}
foreach ($taskHeader in $taskHeaders) { $taskHashes[$taskHeader.Name] = (Get-FileHash -LiteralPath $taskHeader.FullName -Algorithm SHA256).Hash }
& $taskCompiler $taskFixture
if ($LASTEXITCODE -ne 0) { throw 'Repeated generation failed.' }
foreach ($taskHeader in $taskHeaders) {
    if ((Get-FileHash -LiteralPath $taskHeader.FullName -Algorithm SHA256).Hash -ne $taskHashes[$taskHeader.Name]) { throw 'Generation is not deterministic.' }
}
if (([IO.File]::ReadAllText((Join-Path $taskScripts 'Long.generated.h')) -split 'source \+=').Count -lt 20) { throw 'Large UTF-8 metadata must use bounded literals.' }
$taskUpper = Join-Path $taskScripts 'Untouched.SCHEMA.JSON'
[IO.File]::WriteAllText($taskUpper, 'unrecognized suffix', $taskUtf8)
& $taskCompiler $taskFixture
if ($LASTEXITCODE -ne 0 -or [IO.File]::ReadAllText($taskUpper) -ne 'unrecognized suffix') { throw 'Unrecognized files must never be overwritten.' }
$taskBad = Join-Path $taskScripts 'Bad.schema.json'
foreach ($taskInvalid in @(
    @{ version = 2; scripts = @{ CycleA = @{ extends = 'CycleB' }; CycleB = @{ extends = 'CycleA' } } },
    @{ version = 2; scripts = @{ Missing = @{ extends = 'Absent' } } },
    @{ version = 1; scripts = @{ Base = @{} } }
)) {
    [IO.File]::WriteAllText($taskBad, ($taskInvalid | ConvertTo-Json -Depth 12), $taskUtf8)
    $taskRejected = $false
    try { & $taskCompiler $taskFixture *> (Join-Path $taskFixture 'expected-error.log'); $taskRejected = $LASTEXITCODE -ne 0 }
    catch { $taskRejected = $true }
    if (!$taskRejected) { throw 'Invalid declaration was accepted.' }
    foreach ($taskHeader in $taskHeaders) {
        if ((Get-FileHash -LiteralPath $taskHeader.FullName -Algorithm SHA256).Hash -ne $taskHashes[$taskHeader.Name]) { throw 'Rejected declaration modified generated output.' }
    }
}
Remove-Item -LiteralPath $taskBad
if ($BuildNative) {
    $taskSource = @'
#include <SceneRuntime/Scripts/ScriptModuleApi.h>
#include "Script.generated.h"
#include "Long.generated.h"
bool Register_Long(void* context, SceneRuntime::RegisterModuleScript add)
{
    auto script = SceneRuntime::GeneratedSchemas::LongSchemaSchema();
    script.update = [](SceneRuntime::ScriptContext&) {};
    return add(context, "LongSchema", &script);
}
'@
    [IO.File]::WriteAllText((Join-Path $taskScripts 'Long.cpp'), $taskSource, $taskUtf8)
    $taskNamesakeSource = @'
#include <SceneRuntime/Scripts/ScriptModuleApi.h>
#include "Script.generated.h"
bool Register_Script(void* context, SceneRuntime::RegisterModuleScript add)
{
    SceneRuntime::ScriptSchema originalType;
    auto script = SceneRuntime::GeneratedSchemas::ScriptSchema();
    static_cast<void>(originalType);
    script.update = [](SceneRuntime::ScriptContext&) {};
    return add(context, "Script", &script);
}
'@
    [IO.File]::WriteAllText((Join-Path $taskScripts 'Script.cpp'), $taskNamesakeSource, $taskUtf8)
    & (Join-Path $taskRoot 'scripts/BuildScripts.ps1') -Configuration $Configuration -Content $taskFixture -WorkRoot $taskFixture -RunId schema-validation
    if (!$? -or $LASTEXITCODE -ne 0) { throw 'Generated large UTF-8 schema did not compile.' }
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $taskInstallation = & $taskVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
    & (Join-Path $taskInstallation 'MSBuild/Current/Bin/MSBuild.exe') (Join-Path $taskRoot 'tests/ScriptSchemaModuleValidation.vcxproj') "/p:Configuration=$Configuration" /p:Platform=x64 /p:PreferredToolArchitecture=x64 /p:BuildProjectReferences=false /m:1 /nr:false /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'Schema probe did not build.' }
    $taskModule = Join-Path $taskScripts "Bin/$Configuration"
    $taskManifest = Get-Content -LiteralPath (Join-Path $taskModule 'module.json') -Raw | ConvertFrom-Json
    & (Join-Path $taskRoot "generated/tests/outputs/x64/$Configuration/ScriptSchemaModuleValidation/ScriptSchemaModuleValidation.exe") --validate (Join-Path $taskModule $taskManifest.file) $taskFixture
    if ($LASTEXITCODE -ne 0) { throw 'Generated native metadata differs from declarations.' }
}
Write-Output 'PASS: canonical schema parsing, cross-file inheritance, deterministic generation, failure preservation and UTF-8 literals.'
