#pragma once
#include <cmath>
#include <array>
#include <algorithm>

namespace Engine
{
    enum class ToneMapping { None,Reinhard,Filmic };
    struct PostEffectSettings
    {
        bool enabled=false,bloomEnabled=true;
        float exposure=0,bloomIntensity=.2f,bloomThreshold=1,bloomRadius=1;
        ToneMapping toneMapping=ToneMapping::Filmic;
        bool autoExposure=false;
        float exposureMinimum=-8,exposureMaximum=8,middleGray=.18f;
        float contrast=1,saturation=1;
        std::array<float,3> colorFilter{1,1,1};
        bool operator==(const PostEffectSettings&) const = default;
        /// <summary>露出・ブルーム・トーンマッピングの設定値を検証します。</summary>
        bool Valid() const
        {
            return Range(exposure,-10,10) && Range(bloomIntensity,0,10) && Range(bloomThreshold,0,100) && Range(bloomRadius,.25f,8) &&
                (toneMapping==ToneMapping::None || toneMapping==ToneMapping::Reinhard || toneMapping==ToneMapping::Filmic) && ValidAutomaticExposure() && ValidGrading();
        }
    private:
        /// <summary>有限の設定値が許可範囲に収まるか確認します。</summary>
        static bool Range(float value,float minimum,float maximum)
        { return std::isfinite(value) && value>=minimum && value<=maximum; }
        /// <summary>測光の露出範囲と中間灰色を検証します。</summary>
        bool ValidAutomaticExposure() const
        { return Range(exposureMinimum,-10,10) && Range(exposureMaximum,exposureMinimum,10) && Range(middleGray,.01f,1); }
        /// <summary>色調整の強度とRGBフィルターを検証します。</summary>
        bool ValidGrading() const
        { return Range(contrast,0,4) && Range(saturation,0,4) && std::ranges::all_of(colorFilter,[](float value) { return Range(value,0,4); }); }
    };
}
