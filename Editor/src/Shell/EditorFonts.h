#pragma once
#include <filesystem>
#include <string>
namespace Editor
{
    class EditorFonts final
    {
    public:
        /// <summary>
        /// 最初の ImGui フレームより前に一度だけ呼び出します。読み込んだフォントデータはアトラスが所有します。
        /// </summary>
        static bool Initialize(const std::filesystem::path& contentRoot, std::string& error);
    };
}
