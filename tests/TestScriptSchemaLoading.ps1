param(
    [ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskJapanese = [string][char]0x65e5 + [char]0x672c + [char]0x8a9e
$taskFixture = Join-Path $taskRoot ('generated/tests/direct schema ' + $taskJapanese + '/' + [Guid]::NewGuid().ToString('N'))
$taskScripts = Join-Path $taskFixture 'Assets/Scripts'
New-Item -ItemType Directory -Path $taskScripts -Force | Out-Null
$taskUtf8 = [Text.UTF8Encoding]::new($false)
$taskBase = @{ version = 1; scripts = @{ Base = @{ parameters = @{ speed = @{ initial = 2; minimum = 0; maximum = 10 } }; data = @{}; purposes = @{} } } }
$taskDerived = @{ version = 2; scripts = @{ Derived = @{ extends = 'Base'; parameters = @{ speed = @{ initial = 4; minimum = 1; maximum = 8 } } } } }
$taskLarge = @{ version = 1; scripts = @{ LongSchema = @{ parameters = @{}; data = @{}; purposes = @{} } } }
foreach ($taskIndex in 0..31) {
    $taskLarge.scripts.LongSchema.data["field$taskIndex"] = @{ kind = 'string'; value = ([string][char]0x8a9e * 1024) }
}
$taskNamesake = @{ version = 1; scripts = @{ Script = @{} } }
foreach ($taskDeclaration in @(
    @{ name = 'Base'; document = $taskBase },
    @{ name = 'Derived'; document = $taskDerived },
    @{ name = 'Long'; document = $taskLarge },
    @{ name = 'Script'; document = $taskNamesake }
)) {
    [IO.File]::WriteAllText((Join-Path $taskScripts ($taskDeclaration.name + '.schema.json')), ($taskDeclaration.document | ConvertTo-Json -Depth 12), $taskUtf8)
}

$taskSource = @'
#include <SceneRuntime/Scripts/ScriptModuleApi.h>
bool Register_Movement(void* context, SceneRuntime::RegisterModuleScript add)
{
    SceneRuntime::RuntimeScriptDefinition movement;
    movement.update = [](SceneRuntime::RuntimeScriptContext& c)
    {
        auto transform = *c.Read().Transform(c.entity);
        transform.position[0] += c.Value("speed") * static_cast<float>(c.seconds);
        if (!c.SetLocalTransform(transform).succeeded) throw std::runtime_error("Invalid native transform");
    };
    SceneRuntime::RuntimeScriptDefinition empty;
    empty.update = [](SceneRuntime::RuntimeScriptContext&) {};
    return add(context, "Derived", &movement) && add(context, "LongSchema", &empty) && add(context, "Script", &empty);
}
'@
[IO.File]::WriteAllText((Join-Path $taskScripts 'Movement.cpp'), $taskSource, $taskUtf8)
. (Join-Path $PSScriptRoot 'ProjectToolTestSupport.ps1')
$taskTool = Get-ProjectTool
& $taskTool build-scripts --engine $taskRoot --configuration $Configuration --content $taskFixture --work-root $taskFixture --run-id direct-json
if ($LASTEXITCODE -ne 0) { throw 'Callback-only module build failed.' }
if (@(Get-ChildItem -LiteralPath $taskScripts -Filter '*.generated.h' -Recurse).Count -ne 0) {
    throw 'Building a script module must not generate schema headers.'
}
$taskManifestPath = Join-Path $taskScripts "Bin/$Configuration/module.json"
$taskManifestBefore = [IO.File]::ReadAllText($taskManifestPath)
$taskManifest = $taskManifestBefore | ConvertFrom-Json
$taskDll = Join-Path (Split-Path $taskManifestPath -Parent) $taskManifest.file
$taskHashBefore = (Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskInstallation = & $taskVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (!$taskInstallation) { throw 'Visual Studio with C++ and MSBuild is required.' }
# DLL構築で確認済みのSDKをそのまま使い、ホストとDLLのビルド識別子を揃えます。
& (Join-Path $taskInstallation 'MSBuild/Current/Bin/MSBuild.exe') (Join-Path $taskRoot 'tests/ScriptSchemaModuleValidation.vcxproj') "/p:Configuration=$Configuration" /p:Platform=x64 /p:PreferredToolArchitecture=x64 /p:BuildProjectReferences=false /m:1 /nr:false /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Host JSON validation build failed.' }
& (Join-Path $taskRoot "generated/tests/outputs/x64/$Configuration/ScriptSchemaModuleValidation/ScriptSchemaModuleValidation.exe") $taskFixture
if ($LASTEXITCODE -ne 0) { throw 'Host JSON validation failed.' }
if ([IO.File]::ReadAllText($taskManifestPath) -cne $taskManifestBefore -or (Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash -ne $taskHashBefore) {
    throw 'JSON reload must not rebuild or change the DLL or manifest.'
}
Write-Output "PASS: $Configuration uses JSON directly without schema generation or DLL recompilation."
