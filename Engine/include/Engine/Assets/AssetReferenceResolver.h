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
        void Visit(std::filesystem::path& path);
        /// <summary>文字列として保存するアセットパスに同じ解決規則を適用します。</summary>
        void Visit(std::string& text);
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
