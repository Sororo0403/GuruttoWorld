param([string]$Content=(Join-Path (Split-Path $PSScriptRoot -Parent) 'Content'))
$ErrorActionPreference='Stop'
$culture=[Globalization.CultureInfo]::InvariantCulture
function Cpp-String([string]$Value) {
    $escaped=[Text.StringBuilder]::new()
    foreach($character in $Value.ToCharArray()) {
        if($character -eq '\') {[void]$escaped.Append('\\')}
        elseif($character -eq '"') {[void]$escaped.Append('\"')}
        elseif([int]$character -eq 10) {[void]$escaped.Append('\n')}
        elseif([int]$character -eq 13) {[void]$escaped.Append('\r')}
        elseif([int]$character -eq 9) {[void]$escaped.Append('\t')}
        elseif([int]$character -lt 32) {[void]$escaped.Append('\'+[Convert]::ToString([int]$character,8).PadLeft(3,'0'))}
        else {[void]$escaped.Append($character)}
    }
    return '"'+$escaped.ToString()+'"'
}
function Cpp-Float($Value) {
    if($Value -isnot [int] -and $Value -isnot [long] -and $Value -isnot [double] -and $Value -isnot [decimal]){throw 'Script schema numbers must be JSON numbers.'}
    $number=[double]$Value
    if([double]::IsNaN($number) -or [double]::IsInfinity($number)){throw 'Script schema numbers must be finite.'}
    $text=$number.ToString('R',$culture)
    if($text -notmatch '[.Ee]'){$text+='.0'}
    return $text+'f'
}
function Cpp-Value($Value,[int]$Depth=0) {
    if($Depth -gt 8){throw 'Script schema nesting exceeds eight.'}
    switch($Value.kind) {
        'number' {return '{'+(Cpp-Float $Value.value)+'}'}
        'bool' {if($Value.value -isnot [bool]){throw 'Invalid schema boolean.'};return '{'+$Value.value.ToString().ToLowerInvariant()+'}'}
        'string' {if($Value.value -isnot [string]){throw 'Invalid schema string.'};return '{std::string('+(Cpp-String $Value.value)+')}'}
        'reference' {if($Value.value -isnot [string]){throw 'Invalid schema reference.'};return '{ScriptObjectReference{'+(Cpp-String $Value.value)+'}}'}
        'array' {
            if($Value.value -isnot [array]){throw 'Invalid schema array.'}
            if(@($Value.value).Count -gt 256){throw 'Script schema array exceeds 256.'}
            $items=@($Value.value | ForEach-Object {Cpp-Value $_ ($Depth+1)})
            return '{ScriptValue::Array{'+($items -join ',')+'}}'
        }
        'object' {
            $items=@($Value.value.PSObject.Properties | ForEach-Object {'{'+(Cpp-String $_.Name)+','+(Cpp-Value $_.Value ($Depth+1))+'}'})
            if($items.Count -gt 64){throw 'Script schema object exceeds 64 fields.'}
            return '{ScriptValue::Object{'+($items -join ',')+'}}'
        }
        default {throw "Unsupported script schema type: $($Value.kind)"}
    }
}
function Generate-Schema([IO.FileInfo]$File) {
    $schema=Get-Content -LiteralPath $File.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    if($schema.version -ne 1){throw 'Unsupported script schema version.'}
    $lines=[Collections.Generic.List[string]]::new()
    $lines.Add('// Generated from '+$File.Name+'. Edit the JSON schema; builds regenerate this file.')
    $lines.Add('#pragma once');$lines.Add('#include <SceneRuntime/ScriptRuntime.h>');$lines.Add('namespace SceneRuntime {')
    foreach($script in $schema.scripts.PSObject.Properties) {
        if($script.Name -notmatch '^[a-zA-Z_][a-zA-Z0-9_]{0,63}$'){throw 'Schema script names must be C++ identifiers.'}
        $lines.Add('inline ScriptDefinition '+$script.Name+'Schema() {');$lines.Add('    ScriptDefinition result;');$lines.Add('    result.fields={')
        foreach($field in $script.Value.parameters.PSObject.Properties) {
            $value=$field.Value
            if($value.minimum -gt $value.initial -or $value.initial -gt $value.maximum){throw "Invalid script parameter range: $($field.Name)"}
            $lines.Add('        {'+(Cpp-String $field.Name)+',{'+(Cpp-Float $value.initial)+','+(Cpp-Float $value.minimum)+','+(Cpp-Float $value.maximum)+'}},')
        }
        $lines.Add('    };');$lines.Add('    result.dataFields={')
        foreach($field in $script.Value.data.PSObject.Properties) {$lines.Add('        {'+(Cpp-String $field.Name)+','+(Cpp-Value $field.Value)+'},')}
        $lines.Add('    };');$lines.Add('    result.dataPurposes={')
        foreach($field in $script.Value.purposes.PSObject.Properties) {
            if($field.Value -ne 'sceneAsset' -or $script.Value.data.($field.Name).kind -ne 'string'){throw "Invalid scene-asset purpose: $($field.Name)"}
            $lines.Add('        {'+(Cpp-String $field.Name)+',ScriptDataPurpose::SceneAsset},')
        }
        $lines.Add('    };');$lines.Add('    return result;');$lines.Add('}')
    }
    $lines.Add('}')
    $path=Join-Path $File.DirectoryName ($File.Name.Replace('.schema.json','.generated.h'))
    $text=($lines -join "`r`n")+"`r`n"
    if(!(Test-Path -LiteralPath $path) -or [IO.File]::ReadAllText($path) -cne $text){[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))}
}
$sourceRoot=Join-Path $Content 'Assets/Scripts'
if(Test-Path -LiteralPath $sourceRoot){Get-ChildItem -LiteralPath $sourceRoot -File -Recurse -Filter '*.schema.json' | ForEach-Object {Generate-Schema $_}}
