// スキーマのJSONから生成します。定義の変更はJSONへ記述してください。
#pragma once
#include <SceneRuntime/Scripts/ScriptApi.h>

namespace SceneRuntime::GeneratedSchemas
{
    /// <summary>宣言の既定値と型を実行定義へ設定します。</summary>
    inline SceneRuntime::ScriptDefinition StreetArrivalSchema()
    {
        static const auto schema = []
        {
            std::string source;
            source.reserve(421);
            source += R"WP1_0({"scripts":{"StreetArrival":{"data":{"farSound":{"kind":"reference","value":""},"nearSound":{"kind":"reference","value":""}},"parameters":{"farTime":{"initial":1.024211049079895,"maximum":2.0,"minimum":0.3799999952316284},"nearTime":{"initial":0.9910529851913452,"maximum":2.0,"minimum":0.3799999952316284},"release":{"initial":0.9200000166893005,"maximum":1.5,"minimum":0.20000000298023224}},"purposes":{}}},"version":1})WP1_0";
            return SceneRuntime::ScriptSchema::Parse(Engine::Json::parse(source)).at("StreetArrival");
        }();
        SceneRuntime::ScriptDefinition result;
        static_cast<SceneRuntime::ScriptSchema&>(result) = schema;
        return result;
    }
}
