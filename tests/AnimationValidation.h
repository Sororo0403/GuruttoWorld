#pragma once
#include "UiValidation.h"
#include "../Editor/src/GameSession.h"
#include <limits>
#if defined(_DEBUG)
#include "../Editor/src/AnimationPanel.h"
#endif

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
        ScenePlacement player; player.id="player"; player.playerController.emplace();
        layout.objects.push_back(player);
        const auto roundtrip=SceneLayout::Parse(layout.Serialize());
        UiValidation::Require(roundtrip.objects.back().playerController==player.playerController,"player controller JSON roundtrip");
        auto invalidPlayer=layout;
        invalidPlayer.objects.back().playerController->moveSpeed=-1;
        bool rejected=false;
        try { invalidPlayer.Serialize(); } catch (const std::exception&) { rejected=true; }
        UiValidation::Require(rejected,"negative player speed rejected");
        const auto saved=layout.Serialize();
        Editor::GameSession session; std::string error;
        UiValidation::Require(session.Play(renderer,root,layout,error),"animated scene initializes in Editor");
        UiValidation::Require(session.Runtime()->MovePlayers(0.1,1,1),"player movement accepted");
        const auto moved=session.Runtime()->World().Layout().objects.back().position;
        UiValidation::Require(std::abs(std::hypot(moved[0],moved[2])-0.5f)<0.0001f,"diagonal player movement normalized");
        UiValidation::Require(!session.Runtime()->MovePlayers(-1,1,0),"negative movement time rejected");
        UiValidation::Require(session.Runtime()->MovePlayers(10,0,1),"long player tick accepted");
        UiValidation::Require(std::abs(session.Runtime()->World().Layout().objects.back().position[2]-moved[2]-0.5f)<0.0001f,"long player tick capped");
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
#if defined(_DEBUG)
        Editor::EditState state;
        auto candidate=layout.objects[layout.objects.size()-2];
        for (int frame=0;frame<2;++frame)
            UiValidation::Require(renderer.Render({0,0,0,1},[](auto*,float){},[&] {
                ImGui::SetNextWindowPos({20,20}); ImGui::SetNextWindowSize({550,600});
                ImGui::Begin("Animation Inspector validation");
                Editor::AnimationPanel::Draw(state,candidate);
                ImGui::End();
            })!=Engine::RenderResult::Failed,"animation Inspector renders consecutive frames without stack imbalance");
        UiValidation::Require(renderer.WaitForIdle(),"animation Inspector resources complete");
#endif
    }
}
