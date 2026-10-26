#pragma once
#include "ToolCommon.h"

namespace ProjectTool
{
    class ScriptBuildSession final
    {
        void* mutex_ = nullptr;

    public:
        /// <summary>同じ出力先・構成のビルドをプロセス間で直列化します。</summary>
        ScriptBuildSession(const Path& output, std::string_view configuration, unsigned timeoutMs = 600000);
        /// <summary>取得済みのビルド権を解放します。</summary>
        ~ScriptBuildSession();
        ScriptBuildSession(const ScriptBuildSession&) = delete;
        ScriptBuildSession& operator=(const ScriptBuildSession&) = delete;
    };

    /// <summary>設置済みのDLLを指すmanifestを原子的に公開します。</summary>
    void PublishScriptManifest(const Path& path, const Json& manifest);
    /// <summary>ゲームのC++処理をビルドし、入力の整合性を確認して公開します。</summary>
    void BuildScripts(const Options& options);
}
