#pragma once
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include "../App/src/Scenes/AuthoredScene.h"
#include "../Editor/src/GameSession.h"
#include <SceneRuntime/ProjectSettings.h>
#include <SceneRuntime/ScriptModule.h>
#include <stdexcept>
#include <cmath>
#include <iostream>
#include "GamePlaybackCapture.h"

namespace GamePlaybackParityValidation
{
    inline void Run(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,bool departuresOnly=false)
    {
        const auto require=[](bool ok,const char* message) {if(!ok) throw std::runtime_error(message);};
        const auto previousLogs=Engine::Log::Recent();const auto firstSequence=previousLogs.empty() ? uint64_t(0) : previousLogs.back().sequence;
        std::string moduleError;if(!SceneRuntime::ScriptModule::Reload(root,moduleError)) throw std::runtime_error("parity source script module reload: "+moduleError);
        const auto directory=std::filesystem::absolute("generated/tests/playback-parity")/std::to_string(GetTickCount64());
        const auto run=[&](const std::string& scene,int frames,bool title,bool pointerTitle=false,bool quitTitle=false,bool startTitle=false) {
            auto playerWorld=std::make_shared<SceneRuntime::SceneEnvironment>();
            auto player=std::make_unique<App::AuthoredScene>(root,scene,playerWorld,false,true,directory/"player.json");
            require(player->Initialize(renderer),"parity player initializes");
            std::string startTarget;
            if(const auto* configuration=SceneRuntime::TitleBindings::Configuration(playerWorld->World().Layout()))
                for(const auto& entry:configuration->entries) if(entry.index==0) startTarget=entry.target;
            Editor::GameSession editor;std::string error;
            require(editor.Play(renderer,root,SceneRuntime::SceneLayout::Load(root/scene,root),error,directory/"editor.json"),"parity editor initializes");
            Engine::InputActions actions;actions.SetBindings(SceneRuntime::ProjectSettings::Load(root).inputActions);
            Engine::Keyboard keyboard;std::array<BYTE,256> keys{};
            Engine::DiagnosticPointer pointer;pointer.x=1250;pointer.y=700;
            const auto compare=[&] {
                const auto editorScene=editor.Runtime()->ActiveSceneName()=="Main" ? scene : editor.Runtime()->ActiveSceneName();
                require(playerWorld->ActiveSceneName()==editorScene,"player/editor actual loaded scene matches each frame");
                const auto& a=playerWorld->Ui();const auto& b=editor.Runtime()->Ui();
                for(const auto& [key,value]:a.values) {
                    const auto found=b.values.find(key);
                    if(found==b.values.end() || std::abs(found->second-value)>=1e-4f) throw std::runtime_error("player/editor UI mismatch: "+key);
                }
                const auto& playerObjects=playerWorld->World().Layout().objects;
                const auto& editorObjects=editor.Runtime()->World().Layout().objects;
                require(playerObjects.size()==editorObjects.size(),"player/editor object counts match");
                for(size_t index=0;index<playerObjects.size();++index) {
                    const auto& first=playerObjects[index];const auto& second=editorObjects[index];
                    require(first.id==second.id,"player/editor object IDs match");
                    for(size_t axis=0;axis<3;++axis)
                        require(std::abs(first.position[axis]-second.position[axis])<1e-4f && std::abs(first.rotation[axis]-second.rotation[axis])<1e-4f,"player/editor transforms match each frame");
                    require(first.material==second.material,"player/editor material transitions match");
                }
            };
            require(player->Draw(renderer)!=Engine::RenderResult::Failed,"parity player draws");
            bool entered=false,returned=false,started=false;int loads=0,quits=0;
            auto previousRevision=playerWorld->SceneRevision();bool startRequested=false;
            for(int frame=0;frame<frames;++frame) {
                keys.fill(0);bool active=true;pointer.x=1250;pointer.y=700;pointer.buttons=0;
                if(startTitle) {
                    if(frame==90) keys[DIK_RETURN]=0x80;
                } else if(quitTitle) {
                    if(frame==90 || frame==92) keys[DIK_DOWN]=0x80;
                    if(frame==94) keys[DIK_RETURN]=0x80;
                    if(frame>=150 && frame<160) active=false;
                } else if(pointerTitle) {
                    if(frame>=90 && frame<=95) {pointer.x=220;pointer.y=540;}
                    if(frame==94) pointer.buttons=1;
                } else if(title) {
                    if(frame==90) keys[DIK_DOWN]=0x80;
                    if(frame>=92 && frame<=235) keys[DIK_RETURN]=0x80;
                    if(frame==250) keys[DIK_ESCAPE]=0x80;
                    if(frame==325) keys[DIK_RETURN]=0x80;
                    if(frame>=140 && frame<150) active=false;
                } else {
                    if(frame>=5 && frame<30) keys[DIK_W]=0x80;
                    if(frame>=10 && frame<35) keys[DIK_D]=0x80;
                    if(frame==18) keys[DIK_SPACE]=0x80;
                    if(frame>=40 && frame<45) active=false;
                }
                keyboard.ApplyDiagnosticState(nullptr,active,keys,pointer);
                if((startTitle && frame==90) || (title && !pointerTitle && !quitTitle && !startTitle && frame==325)) startRequested=true;
                const auto input=Engine::InputActions::Capture(keyboard,nullptr,true);actions.Update(input);
                // These are the host adapters in production: input is staged, then the shared frame tick runs.
                if(active) editor.PrepareInput(actions,true,1.0/60,&input);
                editor.Playback().Pointer(*editor.Runtime(),renderer.GetWidth(),renderer.GetHeight(),{static_cast<float>(pointer.x),static_cast<float>(pointer.y),(pointer.buttons&1)!=0,false,false,0},active);
                editor.Update(1.0/60,active);
                if(const auto command=editor.TakeCommand();command && command->action=="quit") {++quits;require(quitTitle,"unexpected close request in CONFIG/START playback");}
                require(player->Update(1.0/60,keyboard).empty(),"player loads authored scenes internally in shared tick");
                compare();
                for(const auto& entry:Engine::Log::Recent()) if(entry.sequence>firstSequence && entry.level==Engine::LogLevel::Error) throw std::runtime_error("runtime parity logged error: "+entry.text);
                if(title && !pointerTitle && ((startTitle && (frame==145 || frame==245 || frame==275)) || (quitTitle && (frame==145 || frame==240 || frame==285)) || (!quitTitle && !startTitle && (frame==145 || frame==245 || frame==265 || frame==400 || frame==490)))) {
                    const auto name=(startTitle ? "start-only-" : quitTitle ? "quit-" : "")+std::to_string(frame);
                    GamePlaybackCapture::Capture(renderer,*playerWorld,1280,720,"player-"+name);
                    GamePlaybackCapture::Capture(renderer,*editor.Runtime(),1280,720,"editor-"+name);
                }
                const auto current=playerWorld->ActiveSceneName();
                const bool loaded=playerWorld->SceneRevision()!=previousRevision;
                if(loaded) {++loads;previousRevision=playerWorld->SceneRevision();std::cout<<"loaded "<<current<<" frame "<<frame<<'\n';}
                entered=entered || current=="Assets/Scenes/Settings.json";
                returned=returned || (entered && current=="Assets/Scenes/TitleStreet.json");
                started=started || (loaded && startRequested && current==startTarget);
                if(frame%60==0) {
                    require(player->Draw(renderer)!=Engine::RenderResult::Failed,"parity player frame draws");
                    require(renderer.Render({0,0,0,1},[&](auto* commands,float) {editor.Draw(commands,renderer.GetWidth(),renderer.GetHeight());})!=Engine::RenderResult::Failed,"parity editor frame draws");
                }
            }
            if(title && !pointerTitle && !quitTitle && !startTitle) require(entered && returned && started && loads==3,"keyboard CONFIG Settings back START loads actual scenes once");
            if(startTitle) require(started && loads==1 && !entered,"START alone follows actual host frame order and loads gameplay once");
            if(quitTitle) require(quits==1 && loads==0,"QUIT closes exactly once after common presentation without loading another scene");
            if(pointerTitle) require(entered && loads==1,"pointer CONFIG loads independent Settings scene once");
            require(renderer.WaitForIdle(),"parity GPU completes");editor.Stop();
        };
        if(departuresOnly) {run("Assets/Scenes/TitleStreet.json",320,true,false,false,true);return;}
        run("Assets/Scenes/TitleStreet.json",560,true);
        run("Assets/Scenes/TitleStreet.json",350,true,false,true);
        run("Assets/Scenes/TitleStreet.json",240,true,true);
        run("Assets/Scenes/PhysicsPlayground.json",65,false);
    }
}
