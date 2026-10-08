#pragma once
#include <cmath>

namespace Engine
{
    enum class ToneMapping { None,Reinhard,Filmic };
    struct PostEffectSettings
    {
        bool enabled=false,bloomEnabled=true;
        float exposure=0,bloomIntensity=.2f,bloomThreshold=1,bloomRadius=1;
        ToneMapping toneMapping=ToneMapping::Filmic;
        bool operator==(const PostEffectSettings&) const = default;
        /// <summary>露出・ブルーム・トーンマッピングの設定値を検証します。</summary>
        bool Valid() const
        {
            return std::isfinite(exposure) && exposure>=-10 && exposure<=10 &&
                std::isfinite(bloomIntensity) && bloomIntensity>=0 && bloomIntensity<=10 &&
                std::isfinite(bloomThreshold) && bloomThreshold>=0 && bloomThreshold<=100 &&
                std::isfinite(bloomRadius) && bloomRadius>=.25f && bloomRadius<=8 &&
                (toneMapping==ToneMapping::None || toneMapping==ToneMapping::Reinhard || toneMapping==ToneMapping::Filmic);
        }
    };
}
