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
        explicit SceneDocument(std::filesystem::path path) : path_(std::move(path)), savedSource_(ReadSource(path_)) {}
        const std::filesystem::path& Path() const { return path_; }
        void AssetMoved(const std::filesystem::path& source, const std::filesystem::path& destination)
        {
            if (path_.lexically_normal()!=source.lexically_normal()) return;
            const auto baseline=ReadSource(destination);
            path_=destination;
            savedSource_=baseline;
        }
        bool UnsavedNew() const { return unsavedNew_; }
        void Save(const SceneRuntime::SceneLayout& layout)
        {
            if (ReadSource(path_)!=savedSource_)
                throw std::runtime_error("シーンが外部で変更・削除されています。再読み込みするか、別名で保存してください。");
            if (unsavedNew_ && std::filesystem::exists(path_))
                throw std::runtime_error("新規シーンの保存先にファイルが作成されたため、保存を中止しました。");
            layout.Save(path_,!unsavedNew_);
            unsavedNew_=false;
            savedSource_=ReadSource(path_);
        }
        void SaveAs(const SceneRuntime::SceneLayout& layout, std::filesystem::path target, bool overwrite)
        {
            if (!overwrite && std::filesystem::exists(target))
                throw std::runtime_error("保存先が既に存在します。上書きを確認してください。");
            layout.Save(target,overwrite);
            path_=std::move(target);
            unsavedNew_=false;
            savedSource_=ReadSource(path_);
        }
        bool Pending() const { return request_.has_value(); }
        bool NeedsConfirmation() const { return request_ && !ready_; }
        bool Ready() const { return request_ && ready_; }
        void Confirm() { if (request_) ready_=true; }
        void Cancel() { request_.reset(); ready_=false; }
        bool Request(std::filesystem::path path, bool create, bool dirty)
        {
            if (request_) return false;
            request_=RequestData{std::move(path),create};
            ready_=!dirty;
            return true;
        }
        static std::filesystem::path SaveTarget(const std::filesystem::path& root, const std::string& name)
        {
            const auto filename=std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name.data()),name.size()));
            auto extension=filename.extension().string();
            std::transform(extension.begin(),extension.end(),extension.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (name.empty() || filename!=filename.filename() || extension!=".json" ||
                name.find_first_of("<>:\\|?*\"\r\n")!=std::string::npos || name.back()==' ')
                throw std::runtime_error("フォルダーや予約文字を含まない.jsonファイル名を入力してください。");
            return root/"Assets/Scenes"/filename;
        }
        static std::filesystem::path NewTarget(const std::filesystem::path& root, const std::string& name)
        {
            const auto target=SaveTarget(root,name);
            if (std::filesystem::exists(target)) throw std::runtime_error("そのシーンは既に存在します。別の名前を指定してください。");
            return target;
        }
        // Caller must wait for GPU idle and call outside Render. Failures preserve the active document and world.
        bool Apply(SceneRuntime::SceneWorld& world, const std::filesystem::path& root, std::string& error)
        {
            if (!Ready()) return false;
            auto request=std::move(*request_);
            Cancel();
            try
            {
                if (request.create && std::filesystem::exists(request.path))
                    throw std::runtime_error("新規シーンの保存先が既に存在します。別の名前を指定してください。");
                SceneRuntime::SceneLayout empty;
                const bool success=request.create ? world.ReplaceLayout(std::move(empty),root,error) : world.Reload(root,request.path,error);
                if (!success) return false;
                path_=std::move(request.path);
                unsavedNew_=request.create;
                savedSource_=ReadSource(path_);
                return true;
            }
            catch (const std::exception& exception) { error=exception.what(); return false; }
        }
    private:
        static std::optional<std::string> ReadSource(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path)) return std::nullopt;
            std::ifstream input(path,std::ios::binary);
            if (!input) throw std::runtime_error("シーンの保存状態を読み取れません。");
            std::string source{std::istreambuf_iterator<char>(input),{}};
            if (input.bad()) throw std::runtime_error("シーンの保存状態を読み取れません。");
            return source;
        }
        struct RequestData { std::filesystem::path path; bool create=false; };
        std::filesystem::path path_;
        std::optional<std::string> savedSource_;
        std::optional<RequestData> request_;
        bool ready_=false, unsavedNew_=false;
    };
}
