#pragma once
#include <filesystem>
#include <string>
namespace Editor {
class EditorFonts final {
public:
    // Call once before the first ImGui frame; the atlas owns the loaded font data.
    static bool Initialize(const std::filesystem::path& contentRoot,std::string& error);
};
}
