#pragma once
#include <filesystem>

namespace App
{
    struct GameSettings
    {
        int volume = 10;
        bool backgroundMotion = true;
        /// <summary>既存サンプル音の基準音量に設定倍率を掛けます。</summary>
        float SampleVolume() const { return 0.25f * static_cast<float>(volume) / 10.0f; }
        /// <summary>ユーザー領域の設定ファイルのパスを返します。</summary>
        static std::filesystem::path UserPath();
        /// <summary>欠損・不正なファイルでは既定値を返します。</summary>
        static GameSettings Load(const std::filesystem::path& path);
        /// <summary>一時ファイルを書き終えてから置換します。失敗時は false を返します。</summary>
        bool Save(const std::filesystem::path& path) const;
    };
}
