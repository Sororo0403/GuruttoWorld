#pragma once
#include <Engine/Assets/AssetDatabase.h>

namespace Engine
{
    class AssetReferenceResolver final
    {
    public:
        using References = std::map<std::string, std::string>;
        /// <summary>保存済みIDを使う解決、または現在のパスでのID収集を開始します。</summary>
        AssetReferenceResolver(const AssetDatabase& index, const References& references, bool resolve = true)
            : index_(index), references_(references), resolve_(resolve)
        {
        }
        /// <summary>型側で選択されたアセットパスを解決し、保存するIDを収集します。</summary>
        void Visit(std::filesystem::path& path)
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
        /// <summary>文字列として保存するアセットパスに同じ解決規則を適用します。</summary>
        void Visit(std::string& text)
        {
            auto path = AssetDatabase::Path(text);
            if (!AssetDatabase::Valid(path))
            {
                return;
            }
            Visit(path);
            text = AssetDatabase::Text(path);
        }
        /// <summary>走査したアセットだけの保存用ID対応を取得します。</summary>
        const References& Captured() const
        {
            return captured_;
        }

    private:
        const AssetDatabase& index_;
        References references_, captured_;
        bool resolve_;
    };
}
