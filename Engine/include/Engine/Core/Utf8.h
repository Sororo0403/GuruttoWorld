#pragma once
#include <optional>
#include <string_view>
#include <cstdint>

namespace Engine
{
    namespace Detail
    {
        struct Utf8Prefix
        {
            size_t remaining;
            std::uint32_t value, minimum;
        };
        /// <summary>ASCII以外の先頭バイトから後続長と最小の符号位置を取得します。</summary>
        std::optional<Utf8Prefix> ReadUtf8Prefix(unsigned char lead);
    }
    /// <summary>過長表現・サロゲート・範囲外・途中切断を拒否してUTF-8を検証します。NUL自体は許可します。</summary>
    bool ValidUtf8(std::string_view text);
}
