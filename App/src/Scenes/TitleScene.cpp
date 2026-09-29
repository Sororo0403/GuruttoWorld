#include "TitleScene.h"
#include <Engine/Input/Keyboard.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include <imgui.h>
#endif

namespace App
{
    bool TitleScene::Initialize(Engine::DirectX12Renderer&) { return true; }
    std::string TitleScene::Update(double, const Engine::Keyboard& keyboard)
    {
        return keyboard.IsPressed(DIK_RETURN) ? "Game" : "";
    }
    Engine::RenderResult TitleScene::Draw(Engine::DirectX12Renderer& renderer)
    {
        return renderer.Render({ 0.04f, 0.07f, 0.15f, 1.0f }, {}, []()
        {
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
            ImGui::SetNextWindowPos(ImVec2(420, 260), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 160), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Title"))
            {
                ImGui::TextUnformatted("WP1 - Scene sample");
                ImGui::TextUnformatted("Enter: start game");
                ImGui::TextUnformatted("Escape in game: return to title");
            }
            ImGui::End();
#endif
        });
    }
}
