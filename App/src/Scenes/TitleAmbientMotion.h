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
            if (active && std::isfinite(deltaSeconds) && deltaSeconds >= 0.0)
            {
                const int destination = settingsSelected ? 1 : (exitSelected ? 2 : 0);
                if (destination != destination_)
                {
                    // 切り替えた瞬間の視点を始点に固定し、新しい終点へ直接移動します。
                    start_ = pose_;
                    destination_ = destination;
                    target_ = destination == 1 ? Pose{ 0.6f, 2.3f, 7.0f, -0.82f, 0.18f } :
                        destination == 2 ? Pose{ -0.8f, 1.8f, 7.0f, 0.15f, 0.13f } : Home;
                    progress_ = 0.0f;
                }
                const float duration = destination_ == 1 ? 1.4f : 0.65f;
                progress_ = std::min(1.0f, progress_ + static_cast<float>(std::min(deltaSeconds, 0.1)) / duration);
                for (size_t index = 0; index < pose_.size(); ++index)
                    pose_[index] = std::lerp(start_[index], target_[index], Blend(progress_));
            }
            if (enabled && active && std::isfinite(deltaSeconds) && deltaSeconds > 0.0)
                seconds_ = std::fmod(seconds_ + std::min(deltaSeconds, 0.1), 120.0);
        }
        /// <summary>設定施設・出口ゲートへ移動しながら小さく揺れるカメラ座標を返します。</summary>
        std::array<float, 3> CameraPosition() const
        {
            return { pose_[0] + 0.10f * static_cast<float>(std::sin(seconds_ * std::numbers::pi / 12.0)),
                pose_[1] + 0.04f * static_cast<float>(std::sin(seconds_ * std::numbers::pi / 20.0)), pose_[2] };
        }
        std::array<float, 2> CameraRotation() const
        {
            return { pose_[3], pose_[4] };
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
        using Pose = std::array<float, 5>; // 座標XYZ、ヨー、ピッチ。
        static constexpr Pose Home{ -0.8f, 1.8f, -7.0f, 0.03f, 0.13f };
        Pose pose_ = Home;
        Pose start_ = Home;
        Pose target_ = Home;
        int destination_ = 0;
        float progress_ = 1.0f;
        double seconds_ = 0.0;
        bool enabled_ = true;
    };
}
