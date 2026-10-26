#include <Engine/Assets/AssetReferenceResolver.h>

namespace Engine
{
    auto AssetReferenceResolver::Visit(std::filesystem::path& path) -> void
    {
        if (!AssetDatabase::Valid(path))
        {
            return;
        }
        const auto found = references_.find(AssetDatabase::Text(path));
        const auto id = found == references_.end() ? std::string{} : found->second;
        if (resolve_)
        {
            path = index_.Resolve(path, id);
        }
        // 保存準備でも区切りを揃え、候補と確定後で同じ保存結果になるようにします。
        path = AssetDatabase::Path(AssetDatabase::Text(path));
        const auto current = index_.ReferenceId(path);
        if (!resolve_ && !id.empty())
        {
            captured_[AssetDatabase::Text(path)] = id;
        }
        else if (!current.empty())
        {
            captured_[AssetDatabase::Text(path)] = current;
        }
    }

    auto AssetReferenceResolver::Visit(std::string& text) -> void
    {
        auto path = AssetDatabase::Path(text);
        if (!AssetDatabase::Valid(path))
        {
            return;
        }
        Visit(path);
        text = AssetDatabase::Text(path);
    }
}
