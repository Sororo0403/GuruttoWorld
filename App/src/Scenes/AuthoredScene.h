#pragma once
#include <Engine/Scenes/IScene.h>
#include <SceneRuntime/SceneEnvironment.h>
namespace App {
class AuthoredScene final:public Engine::IScene {
public:
    AuthoredScene(std::filesystem::path root,std::filesystem::path scene):root_(std::move(root)),scene_(std::move(scene)) {}
    bool Initialize(Engine::DirectX12Renderer&) override;
    std::string Update(double,const Engine::Keyboard&) override;
    Engine::RenderResult Draw(Engine::DirectX12Renderer&) override;
private:
    std::filesystem::path root_,scene_;
    SceneRuntime::SceneEnvironment environment_;
    unsigned int width_=0,height_=0;
    bool down_=false,ready_=false;
};
}
