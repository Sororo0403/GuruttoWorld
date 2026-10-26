#pragma once
#include <SceneRuntime/Scene/SceneWorld.h>
#include <optional>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <fstream>
#include "DocumentSave.h"

namespace Editor
{
    class SceneDocument final
    {
    public:
        explicit SceneDocument(std::filesystem::path path, std::shared_ptr<const Engine::ContentContext> context = {})
            : data_(std::make_shared<SceneRuntime::SceneDocumentData>(SceneRuntime::SceneLayout{}, std::move(context))),
              path_(std::move(path)), savedSource_(ReadSource(path_))
        {
        }
        /// <summary>履歴用の複製では編集データを共有せず保持します。</summary>
        SceneDocument(const SceneDocument& other)
            : data_(std::make_shared<SceneRuntime::SceneDocumentData>(*other.data_)), path_(other.path_),
              savedSource_(other.savedSource_), request_(other.request_), ready_(other.ready_),
              unsavedNew_(other.unsavedNew_)
        {
        }
        /// <summary>既存の投影先の所有権を保って、保存基準とデータを復元します。</summary>
        SceneDocument& operator=(const SceneDocument& other);
        /// <summary>保存・履歴・Playの複製元となる、描画から独立したデータを所有します。</summary>
        const std::shared_ptr<SceneRuntime::SceneDocumentData>& Data() const
        {
            return data_;
        }
        /// <summary>編集用投影と同じ正本を取得します。</summary>
        const SceneRuntime::SceneLayout& Layout() const
        {
            return data_->Layout();
        }
        class OpenCandidate final
        {
            friend class SceneDocument;
            /// <summary>候補の生成を、一回の取得と解析を行うPrepareOpenに限定します。</summary>
            OpenCandidate() = default;
            std::filesystem::path path_;
            std::optional<std::string> source_;
            SceneRuntime::SceneLayout layout_;
            bool create_ = false;
        };
        /// <summary>解析対象と保存比較基準を同じ一回の読み取りから準備します。</summary>
        static OpenCandidate PrepareOpen(std::filesystem::path path, bool create = false);
        /// <summary>準備済みの候補を投影し、成功後は例外のない交換だけで保存基準を確定します。</summary>
        bool Apply(SceneRuntime::SceneWorld& world, const std::filesystem::path& root, OpenCandidate candidate,
                   std::string& error);
        /// <summary>ファイルの取得・解析・投影と保存基準の確定を一つの操作として実行します。</summary>
        bool Open(SceneRuntime::SceneWorld& world, const std::filesystem::path& root, const std::filesystem::path& path,
                  std::string& error);
        const std::filesystem::path& Path() const
        {
            return path_;
        }
        /// <summary>開いているアセットの移動先へパスを更新します。保存は行わず、外部変更の比較基準も保持します。</summary>
        void AssetMoved(const std::filesystem::path& source, const std::filesystem::path& destination);
        bool UnsavedNew() const
        {
            return unsavedNew_;
        }
        /// <summary>通常保存をファイル取引で確定し、成功した場合だけ保存基準を更新します。</summary>
        void Save(const SceneRuntime::SceneLayout& layout)
        {
            DocumentSave::Apply({PrepareSave(layout)});
        }
        /// <summary>通常保存の競合を検査し、書き込みと保存基準の候補を準備します。</summary>
        DocumentSavePlan PrepareSave(const SceneRuntime::SceneLayout& layout);
        /// <summary>別名保存とドキュメントのパスを同時に確定します。</summary>
        void SaveAs(const SceneRuntime::SceneLayout& layout, std::filesystem::path target, bool overwrite)
        {
            DocumentSave::Apply({PrepareSaveAs(layout, std::move(target), overwrite)});
        }
        /// <summary>別名保存の成功時だけ、パスと保存基準を切り替える候補を作ります。</summary>
        DocumentSavePlan PrepareSaveAs(const SceneRuntime::SceneLayout& layout, std::filesystem::path target,
                                       bool overwrite);
        bool Pending() const
        {
            return request_.has_value();
        }
        bool NeedsConfirmation() const
        {
            return request_ && !ready_;
        }
        bool Ready() const
        {
            return request_ && ready_;
        }
        void Confirm()
        {
            if (request_)
                ready_ = true;
        }
        void Cancel()
        {
            request_.reset();
            ready_ = false;
        }
        /// <summary>Request の処理を実行します。</summary>
        bool Request(std::filesystem::path path, bool create, bool dirty);
        /// <summary>SaveTarget の処理を実行します。</summary>
        static std::filesystem::path SaveTarget(const std::filesystem::path& root, const std::string& name);
        /// <summary>NewTarget の処理を実行します。</summary>
        static std::filesystem::path NewTarget(const std::filesystem::path& root, const std::string& name);
        /// <summary>
        /// 呼び出し側で GPU の完了を待ち、Render の外で呼んでください。失敗時は現在のドキュメントとワールドを保持します。
        /// </summary>
        bool Apply(SceneRuntime::SceneWorld& world, const std::filesystem::path& root, std::string& error);

    private:
        static std::optional<std::string> ReadSource(const std::filesystem::path& path)
        {
            return Engine::FileContents::Read(path);
        }
        struct RequestData
        {
            std::filesystem::path path;
            bool create = false;
        };
        std::shared_ptr<SceneRuntime::SceneDocumentData> data_ = std::make_shared<SceneRuntime::SceneDocumentData>();
        std::filesystem::path path_;
        std::optional<std::string> savedSource_;
        std::optional<RequestData> request_;
        bool ready_ = false, unsavedNew_ = false;
    };
}
