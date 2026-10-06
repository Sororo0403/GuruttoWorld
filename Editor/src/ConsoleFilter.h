#pragma once
#include <Engine/Core/Log.h>
#include <algorithm>
#include <array>
#include <cctype>

namespace Editor
{
    inline bool ConsoleMatches(const Engine::LogEntry& entry, const std::array<bool,4>& levels,
        std::string search)
    {
        const auto index=static_cast<size_t>(entry.level);
        if (index>=levels.size() || !levels[index]) return false;
        const auto lower=[](unsigned char value) { return static_cast<char>(std::tolower(value)); };
        std::transform(search.begin(),search.end(),search.begin(),lower);
        auto text=entry.text;
        std::transform(text.begin(),text.end(),text.begin(),lower);
        return text.find(search)!=std::string::npos;
    }
}
