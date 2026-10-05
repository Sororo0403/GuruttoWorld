#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace App
{
    class TitleAmbientMotion final
    {
    public:
        /// <summary>背景の揺れは有効時だけ、項目間の移動はアクティブ時に進めます。</summary>
        void Update(double deltaSeconds, bool enabled, bool active, bool settingsSelected = false, bool exitSelected = false)
        {
            enabled_ = enabled;
            if (active && std::isfinite(deltaSeconds) && deltaSeconds > 0.0)
            {
                travel_ = std::clamp(travel_ + static_cast<float>(std::min(deltaSeconds, 0.1)) / 1.4f *
                    (settingsSelected ? 1.0f : -1.0f), 0.0f, 1.0f);
                exitTravel_ = std::clamp(exitTravel_ + static_cast<float>(std::min(deltaSeconds, 0.1)) / 0.65f *
                    (exitSelected && !settingsSelected ? 1.0f : -1.0f), 0.0f, 1.0f);
            }
            if (enabled && active && std::isfinite(deltaSeconds) && deltaSeconds > 0.0)
                seconds_ = std::fmod(seconds_ + std::min(deltaSeconds, 0.1), 120.0);
        }
        /// <summary>設定施設・出口ゲートへ移動しながら小さく揺れるカメラ座標を返します。</summary>
        std::array<float, 3> CameraPosition() const
        {
            const float t = Blend(travel_), e = Blend(exitTravel_);
            return { -0.8f + 1.4f * t + 0.10f * static_cast<float>(std::sin(seconds_ * std::numbers::pi / 12.0)),
                1.8f + 0.5f * t + 0.04f * static_cast<float>(std::sin(seconds_ * std::numbers::pi / 20.0)), -7.0f + 14.0f * (t + e) };
        }
        std::array<float, 2> CameraRotation() const
        {
            // 建物の列に並ぶ奥の門を、通りから少し右へ向いて見せます。
            return { 0.03f - 0.85f * Blend(travel_) + 0.12f * Blend(exitTravel_),
                0.13f + 0.05f * Blend(travel_) };
        }
        /// <summary>光の粒を描画するか返します。</summary>
        bool IsEnabled() const { return enabled_; }
        /// <summary>固定24粒の座標と不透明度を返します。各粒は12秒で巡回します。</summary>
        std::array<float, 4> Mote(unsigned int index) const
        {
            const float phase = static_cast<float>(std::fmod(seconds_ / 12.0 + index / 24.0, 1.0));
            const float wave = std::sin(phase * std::numbers::pi_v<float>);
            return { -2.0f + static_cast<float>((index * 7) % 24) / 6.0f + 0.18f * wave,
                0.8f + phase * 3.0f, 0.5f + static_cast<float>((index * 11) % 24) * 0.7f,
                0.32f * wave * wave };
        }
    private:
        static float Blend(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
        float travel_ = 0.0f;
        float exitTravel_ = 0.0f;
        double seconds_ = 0.0;
        bool enabled_ = true;
    };
}
