#pragma once
#include "../Editor/src/GameSession.h"
#include <stdexcept>
#include <cmath>

namespace TitleConfigTransitionValidation
{
    inline void Run(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        const auto require=[](bool success,const char* message) {if(!success) throw std::runtime_error(message);};
        const auto layout=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/TitleStreet.json",root);
        Editor::GameSession session; std::string error;
        require(session.Play(renderer,root,layout,error),"editor config preview initializes");
        Engine::InputActions actions;
        const auto update=[&] {
            session.Title().Update(actions,true,.1,session.Runtime()->Ui());
            require(session.Update(.1,true),"editor config preview updates");
        };
        for(int i=0;i<15;++i) update();
        SceneRuntime::UiEvent event;event.event="menu:1";
        require(session.Title().Activate(event,session.Runtime()->Ui()),"editor CONFIG click handled");
        require(session.Runtime()->Ui().Value("screen")==3,"editor hides controls until transition completes");
        for(int i=0;i<8;++i) update();
        require(session.Runtime()->Ui().Value("screen")==3,"editor transition does not open settings early");
        event.event="settings:0";
        const float volume=session.Runtime()->Ui().Value("volume");
        session.Title().Activate(event,session.Runtime()->Ui());
        require(session.Runtime()->Ui().Value("volume")==volume,"editor ignores clicks during config transition");
        const float paused=session.Runtime()->Ui().Value("configEnterTime");
        require(session.Pause(),"editor config preview pauses");session.Update(.1,true);
        require(session.Runtime()->Ui().Value("configEnterTime")==paused,"editor pause freezes config transition");
        require(session.Step(),"editor config transition supports step");
        require(session.Runtime()->Ui().Value("configEnterTime")>paused,"editor step advances config transition");
        require(session.Play(renderer,root,layout,error),"editor config preview resumes");
        for(int i=0;i<13;++i) update();
        require(session.Runtime()->Ui().Value("screen")==1,"editor opens settings after transition");
        require(session.Runtime()->CameraPosition()[2]>10,"editor camera approaches cog");
        for(const auto& object:session.Runtime()->World().Layout().objects)
            if(object.meshRenderer && object.meshRenderer->model.filename()=="cog-a.obj")
                require(object.material && object.material->color[3]==0 && object.rotation[1]>3,"editor cog turns white and rotates");
        require(renderer.Render({0,0,0,1},[&](auto* commands,float) {session.Draw(commands,1280,720);})!=Engine::RenderResult::Failed,"editor config transition draws");
        event.event="back";require(session.Title().Activate(event,session.Runtime()->Ui()),"editor config back handled");update();
        require(session.Runtime()->Ui().Value("screen")==0,"editor returns to title");
        for(const auto& object:session.Runtime()->World().Layout().objects)
            if(object.meshRenderer && object.meshRenderer->model.filename()=="cog-a.obj")
                require(object.material && object.material->color[3]==1 && object.rotation[1]==0,"editor restores cog color and rotation");
        event.event="menu:1";session.Title().Activate(event,session.Runtime()->Ui());update();
        require(session.Runtime()->Ui().Value("screen")==3,"editor config transition repeats");
        require(renderer.WaitForIdle(),"editor config preview completes GPU work");require(session.Stop(),"editor config preview stops");
        require(session.Play(renderer,root,layout,error),"editor config preview restarts");
        require(session.Runtime()->Ui().Value("configEnterTime")==0,"editor Play resets config transition");
        require(renderer.WaitForIdle(),"editor restart completes GPU work");session.Stop();
    }
}
