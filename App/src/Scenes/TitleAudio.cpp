#include "TitleAudio.h"
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>
namespace App {
void TitleAudio::Update(SceneRuntime::SceneEnvironment& environment,const std::filesystem::path& root,
    const TitleMenu& menu,bool active,double seconds)
{
    if(!attempted_) {
        attempted_=true;
        std::string error;
        if(!environment.StartAudio(root,error)) Engine::Log::Warning(error);
    }
    const float elapsed=std::isfinite(seconds)?static_cast<float>(std::clamp(seconds,0.0,0.1)):0;
    const float fadeDuration=std::clamp(environment.Ui().Value("musicFadeDuration",0.4f),0.01f,10.0f);
    if(active) fade_=std::min(1.0f,fade_+elapsed/fadeDuration);
    const float gain=menu.GetSettings().Gain();
    environment.Ui().values["volumeGain"]=gain;
    environment.Ui().values["musicVolume"]=gain*fade_*(1-menu.TransitionProgress());
    environment.UpdateAudio(active);
    if(active && gain>0) {
        const auto cue=static_cast<size_t>(menu.GetCue());
        if(cue) environment.AudioCue(menu.Configuration().cues[cue-1]);
    }
}
}
