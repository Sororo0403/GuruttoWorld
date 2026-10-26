#include <Engine/Assets/AssetFileRevision.h>

namespace Engine
{
    auto AssetFileRevision::Capture(const std::filesystem::path& path) -> AssetFileRevision
    {
        const auto file =
            CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            std::unique_ptr<void, decltype(&CloseHandle)> guard(file, CloseHandle);
            BY_HANDLE_FILE_INFORMATION info{};
            FILE_BASIC_INFO basic{};
            if (GetFileInformationByHandle(file, &info) &&
                GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic)) &&
                basic.ChangeTime.QuadPart != 0 && (info.nFileIndexHigh != 0 || info.nFileIndexLow != 0))
                return AssetFileRevision{{static_cast<std::uint64_t>(basic.ChangeTime.QuadPart),
                                          static_cast<std::uint64_t>(basic.LastWriteTime.QuadPart),
                                          info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow,
                                          (std::uint64_t{info.nFileSizeHigh} << 32) | info.nFileSizeLow},
                                         {}};
        }
        const auto contents = Engine::AssetSourceStamp::Capture(path);
        if (!contents)
            throw std::runtime_error("素材の変更情報を読み取れません: " + Engine::FileContents::Text(path));
        return AssetFileRevision{{}, contents};
    }
}
