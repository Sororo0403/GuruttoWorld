#pragma once
#include <SceneRuntime/ScriptModuleApi.h>

namespace SceneRuntime
{
    class ScriptModule final
    {
    public:
        /// <summary>現在の実行ファイルと同じCRT・設定のモジュール構成名を取得します。</summary>
        static const char* Configuration();
        /// <summary>プロジェクトのコンパイル済みC++処理を一括検証し、成功時だけ登録を置き換えます。</summary>
        static bool Reload(const std::filesystem::path& content,std::string& error);
        /// <summary>既存ファイルを上書きせず、公開パラメーター付きのC++処理テンプレートを作成します。</summary>
        static bool CreateSource(const std::filesystem::path& content,const std::string& name,std::filesystem::path& created,std::string& error);
    };
}
