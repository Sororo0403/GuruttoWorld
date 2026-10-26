#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <imgui.h>
#include <optional>
#include <utility>
namespace Editor
{
    class BuildPanel final
    {
        HANDLE process_ = nullptr;
        HANDLE job_ = nullptr;
        std::string runId_, status_;
        std::filesystem::path package_, log_, repository_;
        int configuration_ = 0;
        ULONGLONG nextLog_ = 0, started_ = 0;
        std::string tail_, phase_;
        int step_ = 0;
        bool scripts_ = false;
        std::string scriptConfiguration_ = "Development";
        std::optional<bool> completed_;
        const char* Folder() const
        {
            return scripts_ ? "generated/script-builds" : "generated/builds";
        }
        /// <summary>プロジェクトが参照するエンジンのルートを求めます。</summary>
        static std::filesystem::path RepositoryRoot(const std::filesystem::path& content);

        /// <summary>末尾の区切り文字を含むWindows引数を引用します。</summary>
        static std::wstring QuoteArgument(std::wstring_view argument);

    public:
        bool open = false;
        /// <summary>配布ビルドまたはC++ゲーム処理ビルドの操作画面を作ります。</summary>
        explicit BuildPanel(bool scripts = false, const char* configuration = "Development")
            : scripts_(scripts), scriptConfiguration_(configuration)
        {
        }
        /// <summary>前回のビルド結果を一回だけ取得します。</summary>
        std::optional<bool> TakeCompleted()
        {
            return std::exchange(completed_, std::nullopt);
        }
        /// <summary>初期状態を構築します。</summary>
        ~BuildPanel();
        bool Running() const
        {
            return process_ != nullptr;
        }
        /// <summary>実行状態と進捗を取得します。</summary>
        void Poll(const std::filesystem::path& content);
        /// <summary>保留中の操作を取り消します。</summary>
        void Cancel();
        /// <summary>実行中のビルド進捗とログを読み取ります。</summary>
        void ReadProgress(const std::filesystem::path&);
        /// <summary>ビルドのプロセスを起動し、終了まで管理します。</summary>
        void LaunchBuildProcess(const std::filesystem::path& executable, std::wstring command,
                                const std::filesystem::path& repo);

        /// <summary>処理を開始します。</summary>
        void Start(const std::filesystem::path& content);
        /// <summary>進捗とログの編集画面を描画します。</summary>
        void DrawProgressAndLogs();
        /// <summary>編集画面を描画します。</summary>
        void Draw(const std::filesystem::path& content, bool saved);
    };
}
