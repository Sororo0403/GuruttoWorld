#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Models/Object3D.h>

namespace Engine { class DirectX12Renderer; }
namespace SceneRuntime
{
    // 描画と配置データだけを共有します。ゲーム入力・編集UI・演出を持ちません。
    class SceneWorld final
    {
    public:
        bool Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
            const std::filesystem::path& layoutPath, const std::filesystem::path& shaderPath);
        void Draw(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
            const Engine::DirectionalLight& light) const;
        const SceneLayout& Layout() const { return layout_; }
    private:
        SceneLayout layout_;
        Engine::ModelManager models_;
        std::vector<Engine::Object3D> objects_;
    };
}
