#include "GameSettings.h"
#include <Engine/Core/DiagnosticPaths.h>
#include <Windows.h>
#include <fstream>
#include <string>

namespace App
{
    std::filesystem::path GameSettings::UserPath()
    {
        const auto root = Engine::GetDiagnosticsRoot();
        return root.empty() ? std::filesystem::path{} : root / "settings.txt";
    }
    GameSettings GameSettings::Load(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > 256) return {};
        std::ifstream stream(path);
        std::string magic, extra;
        int version = 0, volume = 0, motion = 0;
        if (!(stream >> magic >> version >> volume >> motion) || magic != "WP1_SETTINGS" || version != 1 ||
            volume < 0 || volume > 10 || (motion != 0 && motion != 1) || (stream >> extra)) return {};
        return { volume, motion != 0 };
    }
    bool GameSettings::Save(const std::filesystem::path& path) const
    {
        if (path.empty() || volume < 0 || volume > 10) return false;
        std::error_code error;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
        auto temporary = path;
        temporary += L".tmp";
        {
            std::ofstream stream(temporary, std::ios::trunc);
            stream << "WP1_SETTINGS 1\n" << volume << ' ' << (backgroundMotion ? 1 : 0) << '\n';
            stream.close();
            if (!stream) return false;
        }
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        std::filesystem::remove(temporary, error);
        return false;
    }
}
