#pragma once
#include <SceneRuntime/SceneWorld.h>
#include <optional>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Editor
{
    class SceneDocument final
    {
    public:
        explicit SceneDocument(std::filesystem::path path) : path_(std::move(path)) {}
        const std::filesystem::path& Path() const { return path_; }
        bool UnsavedNew() const { return unsavedNew_; }
        void Save(const SceneRuntime::SceneLayout& layout)
        {
            if (unsavedNew_ && std::filesystem::exists(path_))
                throw std::runtime_error("The new scene destination now exists. Save was cancelled.");
            layout.Save(path_,!unsavedNew_);
            unsavedNew_=false;
        }
        void SaveAs(const SceneRuntime::SceneLayout& layout, std::filesystem::path target, bool overwrite)
        {
            if (!overwrite && std::filesystem::exists(target))
                throw std::runtime_error("The destination exists. Confirm overwrite before saving.");
            layout.Save(target,overwrite);
            path_=std::move(target);
            unsavedNew_=false;
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
                throw std::runtime_error("Enter a .json filename without folders or reserved characters.");
            return root/"Assets/Scenes"/filename;
        }
        static std::filesystem::path NewTarget(const std::filesystem::path& root, const std::string& name)
        {
            const auto target=SaveTarget(root,name);
            if (std::filesystem::exists(target)) throw std::runtime_error("That scene already exists. Choose another name.");
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
                    throw std::runtime_error("The new scene path now exists. Choose another name.");
                SceneRuntime::SceneLayout empty;
                const bool success=request.create ? world.ReplaceLayout(std::move(empty),root,error) : world.Reload(root,request.path,error);
                if (!success) return false;
                path_=std::move(request.path);
                unsavedNew_=request.create;
                return true;
            }
            catch (const std::exception& exception) { error=exception.what(); return false; }
        }
    private:
        struct RequestData { std::filesystem::path path; bool create=false; };
        std::filesystem::path path_;
        std::optional<RequestData> request_;
        bool ready_=false, unsavedNew_=false;
    };
}
