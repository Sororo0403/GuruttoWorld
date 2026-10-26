#pragma once
#include <Engine/Assets/AssetSourceStamp.h>
#include <memory>
#include <cstdint>

namespace Engine
{
    struct AssetFileRevision
    {
        std::array<std::uint64_t, 6> native{};
        std::optional<Engine::AssetSourceStamp> contents;

        /// <summary>書込日時を保持する変更・置換をChangeTimeとファイルIDで検出し、利用不可時は内容を照合します。</summary>
        static AssetFileRevision Capture(const std::filesystem::path& path);
        /// <summary>取得済みの変更情報を比較します。</summary>
        bool operator==(const AssetFileRevision&) const = default;
    };
}
