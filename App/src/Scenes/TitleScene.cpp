#include "TitleScene.h"
#include <utility>
#include <SceneRuntime/ProjectSettings.h>
namespace App
{
    TitleScene::TitleScene(std::filesystem::path root, bool playIntro, std::filesystem::path scene,
                           std::shared_ptr<SceneRuntime::SceneEnvironment> environment, bool simulateInactive)
        : implementation_(root,
                          scene.empty()
                              ? Engine::AssetDatabase::Path(SceneRuntime::ProjectSettings::Load(root).startupScene)
                              : std::move(scene),
                          std::move(environment), simulateInactive, playIntro)
    {
    }
    bool TitleScene::Initialize(Engine::DirectX12Renderer& renderer)
    {
        return implementation_.Initialize(renderer);
    }
    std::string TitleScene::Update(double seconds, const Engine::Keyboard& keyboard)
    {
        return implementation_.Update(seconds, keyboard);
    }
    Engine::RenderResult TitleScene::Draw(Engine::DirectX12Renderer& renderer)
    {
        return implementation_.Draw(renderer);
    }
}
