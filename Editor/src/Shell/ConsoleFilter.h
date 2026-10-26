#pragma once
#include <Engine/Core/Log.h>
#include <algorithm>
#include <array>
#include <cctype>

namespace Editor
{
    /// <summary>ログが表示条件と検索語に一致するかを確認します。</summary>
    bool ConsoleMatches(const Engine::LogEntry& entry, const std::array<bool, 4>& levels, std::string search);
}
