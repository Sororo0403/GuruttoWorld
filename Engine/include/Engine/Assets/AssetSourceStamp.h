#pragma once
#include <Engine/Assets/ContentFiles.h>
#include <Engine/Core/FileContents.h>
#include <bcrypt.h>
#include <array>
#include <optional>

#pragma comment(lib, "Bcrypt.lib")

namespace Engine
{
    struct AssetSourceStamp
    {
        using Digest = std::array<unsigned char, 32>;
        Digest contents{};
        std::optional<Digest> metadata;

        /// <summary>素材と設定の内容を照合し、日時を保持する置換も検出します。読取失敗時は未設定です。</summary>
        static std::optional<AssetSourceStamp> Capture(const std::filesystem::path& path);
        /// <summary>素材と設定の両方が一致する場合だけ同一の取り込み元と判断します。</summary>
        bool operator==(const AssetSourceStamp&) const = default;

    private:
        /// <summary>固定サイズの作業領域でファイル全体のSHA-256を計算します。</summary>
        static std::optional<Digest> Hash(const std::filesystem::path& path);
    };
}
