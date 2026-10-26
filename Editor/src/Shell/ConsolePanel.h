#pragma once
#include "ConsoleFilter.h"
#include <imgui.h>

namespace Editor
{
    class ConsolePanel final
    {
    public:
        /// <summary>編集画面を描画します。</summary>
        void Draw();

    private:
        /// <summary>内容の編集画面を描画します。</summary>
        void DrawContents();
        /// <summary>項目の編集画面を描画します。</summary>
        void DrawEntries(const std::vector<Engine::LogEntry>& entries);
        std::array<bool, 4> levels_{true, true, true, true};
        std::array<char, 256> search_{};
        bool autoScroll_ = true;
        std::uint64_t lastSequence_ = 0;
    };
}
