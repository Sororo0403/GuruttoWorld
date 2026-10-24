#pragma once
#include <Engine/Assets/AssetSourceStamp.h>
#include <memory>
#include <cstdint>

namespace Engine
{
    struct AssetFileRevision
    {
        std::array<std::uint64_t, 6> native{};
        std::optional<Engine::AssetSourceStamp> contents;

        /// <summary>書込日時を保持する変更・置換をChangeTimeとファイルIDで検出し、利用不可時は内容を照合します。</summary>
        static AssetFileRevision Capture(const std::filesystem::path& path)
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
        /// <summary>取得済みの変更情報を比較します。</summary>
        bool operator==(const AssetFileRevision&) const = default;
    };
}
