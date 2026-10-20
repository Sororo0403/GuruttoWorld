#pragma once
#include <SceneRuntime/SceneWorld.h>
#include <optional>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <fstream>

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
        SceneDocument& operator=(const SceneDocument& other)
        {
            if (this == &other)
                return *this;
            *data_ = *other.data_;
            path_ = other.path_;
            savedSource_ = other.savedSource_;
            request_ = other.request_;
            ready_ = other.ready_;
            unsavedNew_ = other.unsavedNew_;
            return *this;
        }
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
        /// <summary>読み込んだ正本を維持して、開いたファイルの保存基準を更新します。</summary>
        void OpenPath(std::filesystem::path path)
        {
            const auto source = ReadSource(path);
            path_ = std::move(path);
            savedSource_ = source;
            unsavedNew_ = false;
            Cancel();
        }
        const std::filesystem::path& Path() const
        {
            return path_;
        }
        void AssetMoved(const std::filesystem::path& source, const std::filesystem::path& destination)
        {
            if (path_.lexically_normal() != source.lexically_normal())
                return;
            path_ = destination;
            // 改名は保存ではないため、外部変更の比較基準を更新しません。
        }
        bool UnsavedNew() const
        {
            return unsavedNew_;
        }
        void Save(const SceneRuntime::SceneLayout& layout)
        {
            if (ReadSource(path_) != savedSource_)
                throw std::runtime_error(
                    "シーンが外部で変更・削除されています。再読み込みするか、別名で保存してください。");
            if (unsavedNew_ && std::filesystem::exists(path_))
                throw std::runtime_error("新規シーンの保存先にファイルが作成されたため、保存を中止しました。");
            layout.Save(path_, !unsavedNew_);
            unsavedNew_ = false;
            savedSource_ = ReadSource(path_);
        }
        void SaveAs(const SceneRuntime::SceneLayout& layout, std::filesystem::path target, bool overwrite)
        {
            if (!overwrite && std::filesystem::exists(target))
                throw std::runtime_error("保存先が既に存在します。上書きを確認してください。");
            layout.Save(target, overwrite);
            path_ = std::move(target);
            unsavedNew_ = false;
            savedSource_ = ReadSource(path_);
        }
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
        bool Request(std::filesystem::path path, bool create, bool dirty)
        {
            if (request_)
                return false;
            request_ = RequestData{std::move(path), create};
            ready_ = !dirty;
            return true;
        }
        static std::filesystem::path SaveTarget(const std::filesystem::path& root, const std::string& name)
        {
            const auto filename =
                std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name.data()), name.size()));
            auto extension = filename.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char value)
                           {
                               return static_cast<char>(std::tolower(value));
                           });
            if (name.empty() || filename != filename.filename() || extension != ".json" ||
                name.find_first_of("<>:\\|?*\"\r\n") != std::string::npos || name.back() == ' ')
                throw std::runtime_error("フォルダーや予約文字を含まない.jsonファイル名を入力してください。");
            return root / "Assets/Scenes" / filename;
        }
        static std::filesystem::path NewTarget(const std::filesystem::path& root, const std::string& name)
        {
            const auto target = SaveTarget(root, name);
            if (std::filesystem::exists(target))
                throw std::runtime_error("そのシーンは既に存在します。別の名前を指定してください。");
            return target;
        }
        /// <summary>
        /// 呼び出し側で GPU の完了を待ち、Render の外で呼んでください。失敗時は現在のドキュメントとワールドを保持します。
        /// </summary>
        bool Apply(SceneRuntime::SceneWorld& world, const std::filesystem::path& root, std::string& error)
        {
            if (!Ready())
                return false;
            auto request = std::move(*request_);
            Cancel();
            try
            {
                if (request.create && std::filesystem::exists(request.path))
                    throw std::runtime_error("新規シーンの保存先が既に存在します。別の名前を指定してください。");
                SceneRuntime::SceneLayout empty;
                const bool success = request.create ? world.ReplaceLayout(std::move(empty), root, error)
                                                    : world.Reload(root, request.path, error, true);
                if (!success)
                    return false;
                path_ = std::move(request.path);
                unsavedNew_ = request.create;
                savedSource_ = ReadSource(path_);
                return true;
            }
            catch (const std::exception& exception)
            {
                error = exception.what();
                return false;
            }
        }

    private:
        static std::optional<std::string> ReadSource(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path))
                return std::nullopt;
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("シーンの保存状態を読み取れません。");
            std::string source{std::istreambuf_iterator<char>(input), {}};
            if (input.bad())
                throw std::runtime_error("シーンの保存状態を読み取れません。");
            return source;
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
