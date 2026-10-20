// スキーマのJSONから生成します。定義の変更はJSONへ記述してください。
#pragma once
#include <SceneRuntime/ScriptRuntime.h>

namespace SceneRuntime::GeneratedSchemas
{
    /// <summary>宣言の既定値と型を実行定義へ設定します。</summary>
    inline SceneRuntime::ScriptDefinition StreetCameraSchema()
    {
        static const auto schema = []
        {
            std::string source;
            source.reserve(351);
            source += R"WP1_0({"scripts":{"StreetCamera":{"data":{"target":{"kind":"reference","value":""}},"parameters":{"distance":{"initial":7.599999904632568,"maximum":10.0,"minimum":6.0},"fixedView":{"initial":0.0,"maximum":1.0,"minimum":0.0},"height":{"initial":4.5,"maximum":6.0,"minimum":3.0},"lead":{"initial":3.0,"maximum":7.0,"minimum":2.0}},"purposes":{}}},"version":1})WP1_0";
            return SceneRuntime::ScriptSchema::Parse(Engine::Json::parse(source)).at("StreetCamera");
        }();
        SceneRuntime::ScriptDefinition result;
        static_cast<SceneRuntime::ScriptSchema&>(result) = schema;
        return result;
    }
    /// <summary>宣言の既定値と型を実行定義へ設定します。</summary>
    inline SceneRuntime::ScriptDefinition StreetGearSchema()
    {
        static const auto schema = []
        {
            std::string source;
            source.reserve(80);
            source += R"WP1_0({"scripts":{"StreetGear":{"data":{},"parameters":{},"purposes":{}}},"version":1})WP1_0";
            return SceneRuntime::ScriptSchema::Parse(Engine::Json::parse(source)).at("StreetGear");
        }();
        SceneRuntime::ScriptDefinition result;
        static_cast<SceneRuntime::ScriptSchema&>(result) = schema;
        return result;
    }
    /// <summary>宣言の既定値と型を実行定義へ設定します。</summary>
    inline SceneRuntime::ScriptDefinition StreetJourneySchema()
    {
        static const auto schema = []
        {
            std::string source;
            source.reserve(83);
            source += R"WP1_0({"scripts":{"StreetJourney":{"data":{},"parameters":{},"purposes":{}}},"version":1})WP1_0";
            return SceneRuntime::ScriptSchema::Parse(Engine::Json::parse(source)).at("StreetJourney");
        }();
        SceneRuntime::ScriptDefinition result;
        static_cast<SceneRuntime::ScriptSchema&>(result) = schema;
        return result;
    }
}
