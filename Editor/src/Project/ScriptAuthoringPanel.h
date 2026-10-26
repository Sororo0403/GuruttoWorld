#pragma once
#include "BuildPanel.h"
#include "../Assets/AssetRevision.h"
#include <SceneRuntime/Scripts/ScriptModule.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

namespace Editor
{
    class ScriptAuthoringPanel final
    {
        using Inventory = std::map<std::filesystem::path, AssetRevision>;
        Inventory inventory_;
        BuildPanel build_{true, SceneRuntime::ScriptModule::Configuration()};
        std::array<char, 65> name_{};
        std::filesystem::path selected_;
        std::string status_;
        ULONGLONG nextScan_ = 0, changedAt_ = 0;
        bool initialized_ = false, needsBuild_ = false, needsInstall_ = false, ready_ = true, buildFailed_ = false,
             installFailed_ = false, reloaded_ = false;
        /// <summary>JSONの変更と再コンパイルが必要なソース変更を分けます。</summary>
        static Inventory Sources(const Inventory& inventory);
        /// <summary>処理ソース・補助ヘッダーとJSON宣言の変更を確認します。</summary>
        static Inventory Scan(const std::filesystem::path& content);
        /// <summary>変更の安定確認と初回コンパイルを予約します。</summary>
        void Observe(const std::filesystem::path& content, ULONGLONG now);
        /// <summary>ソースの選択と外部エディターでの編集を表示します。</summary>
        void DrawSources();

    public:
        bool open = false;
        /// <summary>コンパイル済みで、再読み込み待ちでない場合に再生を許可します。</summary>
        bool Ready() const
        {
            return ready_ && !needsBuild_ && !needsInstall_ && !build_.Running();
        }
        bool Failed() const
        {
            return buildFailed_ || installFailed_;
        }
        /// <summary>定義の再読み込み成功を一度だけ通知し、Inspectorの宣言更新につなげます。</summary>
        bool TakeReloaded();
        /// <summary>変更が安定したソースをコンパイルし、停止中に結果を読み込みます。</summary>
        void Poll(const std::filesystem::path& content, bool editing);
        /// <summary>処理の作成、ソース編集、コンパイル診断を表示します。</summary>
        void Draw(const std::filesystem::path& content);
    };
}
