#include "SceneDocument.h"
#include <Engine/Core/FileContents.h>

namespace Editor
{
    std::optional<std::string> SceneDocument::ReadSource(const std::filesystem::path& path)
    {
        return Engine::FileContents::Read(path);
    }

    auto SceneDocument::operator=(const SceneDocument& other) -> SceneDocument&
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

    auto SceneDocument::PrepareOpen(std::filesystem::path path, bool create) -> OpenCandidate
    {
        OpenCandidate candidate;
        candidate.path_ = std::move(path);
        candidate.create_ = create;
        candidate.source_ = ReadSource(candidate.path_);
        if (create)
        {
            if (candidate.source_)
                throw std::runtime_error("新規シーンの保存先が既に存在します。別の名前を指定してください。");
        }
        else
        {
            if (!candidate.source_)
                throw std::runtime_error("Cannot open layout: " + candidate.path_.string());
            candidate.layout_ = SceneRuntime::SceneLayout::Parse(*candidate.source_);
        }
        return candidate;
    }

    bool SceneDocument::Apply(OpenCandidate candidate, std::string& error)
    {
        if (!data_->Edit(SceneRuntime::ReplaceDocumentEdit{std::move(candidate.layout_)}, error).succeeded)
            return false;
        path_.swap(candidate.path_);
        savedSource_.swap(candidate.source_);
        unsavedNew_ = candidate.create_;
        Cancel();
        return true;
    }

    bool SceneDocument::Open(const std::filesystem::path& path, std::string& error)
    {
        try
        {
            return Apply(PrepareOpen(path), error);
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }

    auto SceneDocument::AssetMoved(const std::filesystem::path& source, const std::filesystem::path& destination)
        -> void
    {
        if (path_.lexically_normal() != source.lexically_normal())
            return;
        path_ = destination;
    }

    auto SceneDocument::PrepareSave(const SceneRuntime::SceneLayout& layout) -> DocumentSavePlan
    {
        auto plan = PrepareSaveAs(layout, path_, !unsavedNew_);
        if (plan.file.before != savedSource_)
            throw std::runtime_error(
                "シーンが外部で変更・削除されています。再読み込みするか、別名で保存してください。");
        return plan;
    }

    auto SceneDocument::PrepareSaveAs(const SceneRuntime::SceneLayout& layout, std::filesystem::path target,
                                      bool overwrite) -> DocumentSavePlan
    {
        auto file = FileTransaction::Prepare(target, layout.SerializeForSave(target));
        if (!overwrite && file.before)
            throw std::runtime_error("保存先が既に存在します。上書きを確認してください。");
        auto accept = [this, target = std::move(target), source = file.after]() mutable noexcept
        {
            path_.swap(target);
            savedSource_.swap(source);
            unsavedNew_ = false;
        };
        return {std::move(file), std::move(accept)};
    }

    auto SceneDocument::Request(std::filesystem::path path, bool create, bool dirty) -> bool
    {
        if (request_)
            return false;
        request_ = RequestData{std::move(path), create};
        ready_ = !dirty;
        return true;
    }

    auto SceneDocument::SaveTarget(const std::filesystem::path& root, const std::string& name) -> std::filesystem::path
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

    auto SceneDocument::NewTarget(const std::filesystem::path& root, const std::string& name) -> std::filesystem::path
    {
        const auto target = SaveTarget(root, name);
        if (std::filesystem::exists(target))
            throw std::runtime_error("そのシーンは既に存在します。別の名前を指定してください。");
        return target;
    }

}
