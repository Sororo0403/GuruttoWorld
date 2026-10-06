#pragma once
#include <SceneRuntime/SceneEnvironment.h>
#include "TitleMenu.h"
namespace App {
class TitleAudio final {
public:
    void Update(SceneRuntime::SceneEnvironment&,const std::filesystem::path& root,const TitleMenu&,bool active,double seconds);
private:
    bool attempted_=false;
    float fade_=0;
};
}
