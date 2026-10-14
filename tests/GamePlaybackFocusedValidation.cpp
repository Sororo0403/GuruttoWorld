#include "GamePlaybackParityValidation.h"
#include "TitleConfigTransitionValidation.h"
#include "TitleDepartureValidation.h"
#include <Engine/Platform/Window.h>
#include <Engine/Core/Log.h>
#include <iostream>
#include <SceneRuntime/ScriptModule.h>
#include "../Editor/src/MaterialDocument.h"
int main() {
    try {
        Engine::Log::Initialize("generated/tests/playback-focused.log");
        Engine::Window window;Engine::DirectX12Renderer renderer;
        if(!window.Create(L"Playback parity validation",1280,720) || !renderer.Initialize(window.GetHandle())) throw std::runtime_error("renderer initialization failed");
        const auto root=std::filesystem::absolute("Games/GuruttoWorld");
        const auto materialRoot=std::filesystem::absolute("generated/tests/playback-material")/std::to_string(GetTickCount64());
        const std::filesystem::path materialPath="Assets/Materials/effect.mat";SceneRuntime::MaterialAsset material;material.Save(materialRoot,materialPath);
        Editor::MaterialDocument draft;draft.Reload(materialRoot,materialPath);draft.Asset().values.effectAmount=.75f;draft.Observe({});
        if(!draft.Dirty() || !draft.Undo() || draft.Dirty() || draft.Asset().values.effectAmount!=0 || !draft.Undo(true) || !draft.Dirty() || draft.Asset().values.effectAmount!=.75f) throw std::runtime_error("effectAmount-only material Dirty/Undo/Redo regression");
        draft.Save(materialRoot,materialPath);
        if(draft.Dirty() || SceneRuntime::MaterialAsset::Load(materialRoot,materialPath).values.effectAmount!=.75f) throw std::runtime_error("effectAmount material save roundtrip regression");
        draft.Asset().values.effectVariant=2;draft.Observe({});
        if(!draft.Dirty() || !draft.Undo() || draft.Dirty() || draft.Asset().values.effectVariant!=0 || !draft.Undo(true) || draft.Asset().values.effectVariant!=2) throw std::runtime_error("effectVariant-only material Dirty/Undo/Redo regression");
        draft.Save(materialRoot,materialPath);
        if(draft.Dirty() || SceneRuntime::MaterialAsset::Load(materialRoot,materialPath).values.effectVariant!=2) throw std::runtime_error("effectVariant material save roundtrip regression");
        GamePlaybackParityValidation::Run(renderer,root);
        TitleConfigTransitionValidation::Run(renderer,root);
        TitleDepartureValidation::Run(renderer,root);
        if(!renderer.WaitForIdle()) throw std::runtime_error("GPU completion failed");
        for(const auto& entry:Engine::Log::Recent()) if(entry.level==Engine::LogLevel::Error) throw std::runtime_error("runtime logged error: "+entry.text);
        std::cout<<"PASS: actual App/Editor frame parity, CONFIG independent scene, smooth return, START, pointer, pause/step/resume/restart, isolated settings, opaque silhouette, START white buildings/black windows road travel, QUIT black/white arrow travel\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        for(const auto& entry:Engine::Log::Recent()) if(entry.level==Engine::LogLevel::Error || entry.level==Engine::LogLevel::Warning) std::cerr<<entry.text<<'\n';
        return 1;
    }
}
