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
        /// <summary>有効かつアクティブな時間だけ進めます。OFF時は現在の視点で停止します。</summary>
        void Update(double deltaSeconds, bool enabled, bool active)
        {
            enabled_ = enabled;
            if (enabled && active && std::isfinite(deltaSeconds) && deltaSeconds > 0.0)
                seconds_ = std::fmod(seconds_ + std::min(deltaSeconds, 0.1), 120.0);
        }
        /// <summary>構図の基準位置から小さく動くカメラ座標を返します。</summary>
        std::array<float, 3> CameraPosition() const
        {
            return { -0.8f + 0.10f * static_cast<float>(std::sin(seconds_ * std::numbers::pi / 12.0)),
                1.8f + 0.04f * static_cast<float>(std::sin(seconds_ * std::numbers::pi / 20.0)), -7.0f };
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
        double seconds_ = 0.0;
        bool enabled_ = true;
    };
}
