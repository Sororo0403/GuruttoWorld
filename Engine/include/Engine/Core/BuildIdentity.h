#pragma once
#include <cstdint>
#include <filesystem>

namespace Engine
{
    /// <summary>このライブラリを構築した共通ソース・設定の識別子を返します。</summary>
    std::uint64_t BuildIdentity();
    /// <summary>ビルド時に記録したソース参照先を返します。ヘッダーの配置階層には依存しません。</summary>
    std::filesystem::path BuildSourceRoot();
}
