#pragma once
#include <SceneRuntime/Rendering/RenderPacket.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <Engine/Graphics/Renderers/ShadowMap.h>

namespace SceneRuntime
{
    struct GpuRenderObject final : RenderResource
    {
        Engine::Object3D object;
        std::vector<std::shared_ptr<const Engine::ModelRenderer>> levels;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
    };
    struct GpuRenderScene final : RenderResource
    {
        std::shared_ptr<Engine::ShadowMap> shadow, localShadow;
    };
}
