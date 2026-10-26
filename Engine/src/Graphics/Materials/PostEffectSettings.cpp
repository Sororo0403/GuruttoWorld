#include <Engine/Graphics/Materials/PostEffectSettings.h>

namespace Engine
{
    auto PostEffectSettings::Valid() const -> bool
    {
        return Range(exposure, -10, 10) && Range(bloomIntensity, 0, 10) && Range(bloomThreshold, 0, 100) &&
               Range(bloomRadius, .25f, 8) &&
               (toneMapping == ToneMapping::None || toneMapping == ToneMapping::Reinhard ||
                toneMapping == ToneMapping::Filmic) &&
               ValidAutomaticExposure() && ValidGrading();
    }

    auto PostEffectSettings::ValidGrading() const -> bool
    {
        return Range(contrast, 0, 4) && Range(saturation, 0, 4) &&
               std::ranges::all_of(colorFilter,
                                   [](float value)
                                   {
                                       return Range(value, 0, 4);
                                   });
    }
}
