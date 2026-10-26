#include <Engine/Core/Utf8.h>

namespace Engine::Detail
{
    std::optional<Utf8Prefix> ReadUtf8Prefix(unsigned char lead)
    {
        if (lead < 0xc2)
            return std::nullopt;
        if (lead <= 0xdf)
            return Utf8Prefix{1, static_cast<std::uint32_t>(lead & 0x1f), 0x80};
        if (lead <= 0xef)
            return Utf8Prefix{2, static_cast<std::uint32_t>(lead & 0x0f), 0x800};
        if (lead <= 0xf4)
            return Utf8Prefix{3, static_cast<std::uint32_t>(lead & 7), 0x10000};
        return std::nullopt;
    }
}

namespace Engine
{
    bool ValidUtf8(std::string_view text)
    {
        for (size_t index = 0; index < text.size();)
        {
            const auto lead = static_cast<unsigned char>(text[index++]);
            if (lead < 0x80)
                continue;
            auto prefix = Detail::ReadUtf8Prefix(lead);
            if (!prefix || prefix->remaining > text.size() - index)
                return false;
            for (size_t byte = 0; byte < prefix->remaining; ++byte)
            {
                const auto next = static_cast<unsigned char>(text[index++]);
                if ((next & 0xc0) != 0x80)
                    return false;
                prefix->value = (prefix->value << 6) | (next & 0x3f);
            }
            if (prefix->value < prefix->minimum || prefix->value > 0x10ffff ||
                (prefix->value >= 0xd800 && prefix->value <= 0xdfff))
                return false;
        }
        return true;
    }
}
