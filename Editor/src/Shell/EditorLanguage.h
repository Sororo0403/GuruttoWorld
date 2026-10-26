#pragma once
#include <imgui_internal.h>
#include <cstdio>
#include <string>

namespace Editor::Language
{
    /// <summary>初期化します。</summary>
    void Initialize();

    /// <summary>指定した内容へ置き換えます。</summary>
    void Replace(std::string& text, const std::string& from, const std::string& to);

    /// <summary>
    /// 旧形式の通常名と ### 付きの名前ではハッシュ値が異なるため、ドックノード・位置・サイズを保持して、
    /// ウィンドウのセクションと選択中のタブ ID を移行します。
    /// </summary>
    std::string MigrateLayout(std::string text);
}
