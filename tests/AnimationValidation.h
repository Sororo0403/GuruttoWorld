#pragma once
#include "UiValidation.h"
#include "../Editor/src/GameSession.h"
#include <limits>

namespace AnimationValidation
{
    inline void Schema()
    {
        using namespace SceneRuntime;
        AnimationTrack track;
        track.property="uiPosition"; track.easing="linear";
        track.keys={{0,{0,0,0,0}},{1,{32,16,0,0}},{2,{0,0,0,0}}};
        UiValidation::Require(Animation::Valid(track),"valid animation accepted");
        UiValidation::Require(!Animation::Sample(track,{{"sceneTime",-1.0f}}),"unstarted clock does not apply first key");
        UiValidation::Require(Animation::Sample(track,{{"sceneTime",0.5f}})->at(0)==16,"animation interpolates between keys");
        track.loop=true;
        UiValidation::Require(Animation::Sample(track,{{"sceneTime",2.5f}})->at(0)==16,"loop retains remainder");
        auto invalid=track; invalid.keys[1].time=0;
        UiValidation::Require(!Animation::Valid(invalid),"duplicate times rejected");
        invalid=track; invalid.keys[0].value[0]=std::numeric_limits<float>::infinity();
        UiValidation::Require(!Animation::Valid(invalid),"nonfinite key rejected");
        auto layout=UiValidation::Layout();
        layout.objects[1].animation=AnimationComponent{"animation",true,{track}};
        const auto restored=SceneLayout::Parse(layout.Serialize());
        UiValidation::Require(restored.objects[1].animation==layout.objects[1].animation,"animation JSON roundtrip preserves tracks");
        UiState state; state.values["sceneTime"]=0.5f;
        const auto rect=SceneUi::Resolve(layout,layout.objects[1],128,64,state);
        UiValidation::Require(rect.position==std::array<float,2>{32,16},"animated UI follows Canvas scale");
        UiValidation::Require(SceneUi::Hit(layout,128,64,40,20,state)=="panel","hit testing follows animated UI");
        invalid=track; invalid.property="unsupported";
        layout.objects[1].animation->tracks={invalid};
        bool rejected=false; try {static_cast<void>(layout.Serialize());} catch (...) {rejected=true;}
        UiValidation::Require(rejected,"unknown animation property rejected atomically");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        using namespace SceneRuntime;
        auto layout=UiValidation::Layout(); layout.objects.back().audioSource.reset();
        ScenePlacement camera; camera.id="camera"; camera.camera.emplace();
        AnimationTrack track; track.property="position"; track.easing="linear";
        track.keys={{0,{0,0,-10,0}},{1,{4,2,-8,0}}};
        camera.animation=AnimationComponent{"animation",true,{track}};
        layout.objects.push_back(camera);
        const auto saved=layout.Serialize();
        Editor::GameSession session; std::string error;
        UiValidation::Require(session.Play(renderer,root,layout,error),"animated scene initializes in Editor");
        session.Runtime()->SeekAnimation(0.5f,0.5f);
        UiValidation::Require(session.Runtime()->CameraPosition()==std::array<float,3>{2,1,-9},"camera keyframe preview");
        session.Pause();
        const auto before=session.Runtime()->CameraPosition();
        session.Update(0.1,true);
        UiValidation::Require(session.Runtime()->CameraPosition()==before,"pause freezes camera animation");
        session.Step();
        UiValidation::Require(session.Runtime()->CameraPosition()[0]>before[0],"step advances camera animation");
        session.Runtime()->Ui().values["startRequested"]=1;
        session.Step(); session.Step();
        UiValidation::Require(session.Runtime()->Ui().Value("startTime")>0,"start trigger advances shared clock");
        session.Runtime()->SeekAnimation(0,0,-1);
        UiValidation::Require(session.Runtime()->CameraPosition()==std::array<float,3>{0,0,-10},"seeking backward restores camera pose");
        session.Stop();
        UiValidation::Require(layout.Serialize()==saved,"preview never changes authored scene");
    }
}
