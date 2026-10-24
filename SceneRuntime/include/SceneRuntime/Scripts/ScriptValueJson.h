#pragma once
#include <SceneRuntime/Scripts/ScriptValue.h>
#include <Engine/Core/Json.h>

namespace SceneRuntime
{
    /// <summary>深さと要素数の上限を適用し、保存形式から型付きデータを読み取ります。</summary>
    ScriptValue ReadScriptValue(const Engine::Json& source, size_t depth, size_t& remaining);
    /// <summary>型付きデータをDLLやスクリプト登録に依存せず保存形式へ変換します。</summary>
    Engine::Json WriteScriptValue(const ScriptValue& source);
}
