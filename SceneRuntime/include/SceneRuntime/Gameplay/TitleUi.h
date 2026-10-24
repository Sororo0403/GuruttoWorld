#pragma once
#include <SceneRuntime/Ui/SceneUi.h>
#include <SceneRuntime/Gameplay/TitleMenu.h>
namespace SceneRuntime
{
    class TitleUi final
    {
    public:
        bool Initialize(const Engine::DirectX12Renderer&, const std::filesystem::path& root,
                        const std::filesystem::path& scene = {});
        void Draw(ID3D12GraphicsCommandList*, unsigned int width, unsigned int height, const TitleMenu&) const;
        static SceneRuntime::UiState State(const TitleMenu&, const SceneRuntime::UiState& settings = {});

    private:
        SceneRuntime::SceneLayout layout_;
        SceneRuntime::SceneUi ui_;
    };
}
