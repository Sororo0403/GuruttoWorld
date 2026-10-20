#pragma once
#include <filesystem>
#include <Engine/Assets/ContentContext.h>
#include <map>
#include <string>

namespace SceneRuntime
{
    struct GameSettings
    {
        int volume = 10;
        bool backgroundMotion = true;
        std::map<std::string, float> values;
        float volumeGain = -1;
        bool loaded = false;
        float Gain() const
        {
            return volumeGain >= 0 ? volumeGain : static_cast<float>(volume) / 10.0f;
        }
        /// <summary>既存サンプル音の基準音量に設定倍率を掛けます。</summary>
        float SampleVolume() const
        {
            return 0.25f * Gain();
        }
        /// <summary>プロジェクトのアセット ID ごとに保存先を分離し、明示された旧設定を初回だけ引き継ぎます。</summary>
        static std::filesystem::path UserPath(const std::filesystem::path& content);
        /// <summary>指定したユーザー領域を使い、実際の個人設定に触れず保存先と移行を検証できます。</summary>
        static std::filesystem::path UserPath(const std::filesystem::path& content,
                                              const std::filesystem::path& userRoot);
        /// <summary>実行開始時に固定したIDで設定保存先を解決します。</summary>
        static std::filesystem::path UserPath(const Engine::ContentContext& content,
                                              const std::filesystem::path& userRoot);
        /// <summary>固定した参照情報から通常のユーザー保存先を取得します。</summary>
        static std::filesystem::path UserPath(const Engine::ContentContext& content);
        /// <summary>欠損・不正なファイルでは既定値を返します。</summary>
        static GameSettings Load(const std::filesystem::path& path);
        /// <summary>一時ファイルを書き終えてから置換します。失敗時は false を返します。</summary>
        bool Save(const std::filesystem::path& path) const;
    };
}
