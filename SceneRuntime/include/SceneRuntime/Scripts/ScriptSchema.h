#pragma once
#include <SceneRuntime/Scripts/ScriptValue.h>
#include <Engine/Core/Json.h>
#include <filesystem>

namespace SceneRuntime
{
    struct ScriptComponent;
    enum class ScriptDataPurpose
    {
        Text,
        SceneAsset
    };
    struct ScriptField
    {
        float initial = 0, minimum = -1000000, maximum = 1000000;
        bool operator==(const ScriptField&) const = default;
    };
    struct ScriptMigrationReport
    {
        std::vector<std::string> changes;
        std::vector<std::string> conflicts;
        std::vector<std::string> unavailable;
    };
    struct ScriptSchema
    {
        std::map<std::string, ScriptField> fields;
        ScriptValue::Object dataFields;
        std::map<std::string, ScriptDataPurpose> dataPurposes;
        bool operator==(const ScriptSchema&) const = default;
        /// <summary>保存・インスペクター・実行が共有するスキーマの制約を検証します。</summary>
        void Validate(const std::string& name) const;
        /// <summary>明示的な追加・リセット操作で既定値と素材参照の指定を設定します。</summary>
        void ResetValues(ScriptComponent& component) const;
        /// <summary>未知の値を保持して不足分を補います。競合時は全体を変更せず、診断を返します。</summary>
        ScriptMigrationReport MigrateValues(ScriptComponent& component) const;
        /// <summary>既存のschema.jsonを、実行コールバックを読み込まず解釈します。</summary>
        static std::map<std::string, ScriptSchema> Parse(const Engine::Json& source);
        /// <summary>検証済みの既定値・型・参照用途を宣言用JSONへ変換します。</summary>
        Engine::Json Serialize() const;
        /// <summary>DLLの登録に依存しない組込動作の宣言を取得します。</summary>
        static const std::map<std::string, ScriptSchema>& Builtins();
        /// <summary>プロジェクト全体の継承を解決し、重複・欠損した基底・循環を拒否します。</summary>
        static std::map<std::string, ScriptSchema> LoadProject(const std::filesystem::path& root);
    };
}
