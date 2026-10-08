#include <Engine/Graphics/Renderers/MeshRenderer.h>
#include <Engine/Graphics/Resources/IndexedMeshBuffer.h>
#include <Engine/Graphics/Resources/RenderTargetBinding.h>
#include <Engine/Core/Log.h>

#include <algorithm>
#include <utility>
#include <format>
#include <cmath>

namespace Engine
{
    ID3D12DescriptorHeap* MeshRenderer::Bindings(const ShadowMap* shadow,const std::shared_ptr<const Texture2D>& overrideTexture,
        const std::shared_ptr<const Texture2D>& normal,const LocalLightView& lights,const SkinPaletteView& palette) const
    {
        const std::shared_ptr<const Texture2D> texture=overrideTexture ? overrideTexture : texture_;
        const auto key=std::tuple{shadow ? shadow->Resource() : nullptr,texture.get(),normal.get(),lights.resource,palette.resource};
        if (const auto found=bindings_.find(key);found!=bindings_.end()) return found->second.heap.Get();
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(meshBuffer_->GetDevice(IID_PPV_ARGS(&device)))) return nullptr;
        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        description.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; description.NumDescriptors=5;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        if (FAILED(device->CreateDescriptorHeap(&description,IID_PPV_ARGS(&heap)))) return nullptr;
        auto handle=heap->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1,handle,texture->GetShaderResourceView(),description.Type);
        handle.ptr+=device->GetDescriptorHandleIncrementSize(description.Type);
        if (shadow) device->CopyDescriptorsSimple(1,handle,shadow->View(),description.Type);
        else {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format=DXGI_FORMAT_R32_FLOAT; view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(nullptr,&view,handle);
        }
        handle.ptr+=device->GetDescriptorHandleIncrementSize(description.Type);
        if (normal) device->CopyDescriptorsSimple(1,handle,normal->GetShaderResourceView(),description.Type);
        else {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format=DXGI_FORMAT_R8G8B8A8_UNORM; view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(nullptr,&view,handle);
        }
        handle.ptr+=device->GetDescriptorHandleIncrementSize(description.Type);
        if (lights.resource) device->CopyDescriptorsSimple(1,handle,lights.srv,description.Type);
        else {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Buffer.NumElements=1; view.Buffer.StructureByteStride=16;
            device->CreateShaderResourceView(nullptr,&view,handle);
        }
        handle.ptr+=device->GetDescriptorHandleIncrementSize(description.Type);
        if (palette.resource) device->CopyDescriptorsSimple(1,handle,palette.srv,description.Type);
        else {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Buffer.NumElements=1; view.Buffer.StructureByteStride=sizeof(SkinMatrix);
            device->CreateShaderResourceView(nullptr,&view,handle);
        }
        // 各組み合わせを保持し、実行中のフレームが参照する SRV を書き換えません。
        return bindings_.emplace(key,Binding{std::move(heap),texture,normal}).first->second.heap.Get();
    }
    void MeshRenderer::DrawShadow(ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& world,const ShadowMap& shadow,std::span<const SkinMatrix> palette) const
    {
        if (!initialized_ || !commands || (!palette.empty() && palette.size()<requiredPaletteSize_)) return;
        const auto skin=palette.empty() ? SkinPaletteView{} : resources_->PrepareSkin(commands,palette);
        if (!palette.empty() && !skin.resource) { Log::Error("Cannot prepare shadow skin palette."); return; }
        auto* heap=Bindings(nullptr,nullptr,nullptr,{},skin); if (!heap) return;
        auto handle=heap->GetGPUDescriptorHandleForHeapStart();
        Microsoft::WRL::ComPtr<ID3D12Device> device; if (FAILED(meshBuffer_->GetDevice(IID_PPV_ARGS(&device)))) return;
        handle.ptr+=4ULL*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        commands->SetDescriptorHeaps(1,&heap); shadow.Draw(commands,world,vertexView_,indexView_,indexCount_,handle);
    }
    bool MeshRenderer::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
        const MeshData& mesh, const std::filesystem::path& shaderPath)
    {
        if (initialized_ || device == nullptr || queue == nullptr)
        {
            return false;
        }
        auto resources = std::make_shared<MeshResources>();
        return resources->Initialize(device, shaderPath) && Initialize(device, queue, mesh, resources);
    }

    bool MeshRenderer::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
        const MeshData& mesh, const std::shared_ptr<MeshResources>& resources)
    {
        if (initialized_ || device == nullptr || queue == nullptr || !resources ||
            resources->GetPipelineState() == nullptr || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            return false;
        }
        size_t required=1;
        for (const auto& vertex : mesh.vertices)
            for (size_t influence=0;influence<4;++influence)
            {
                const float weight=vertex.weights[influence];
                if (!std::isfinite(weight) || weight<0 || (weight>0 && vertex.joints[influence]>=256)) return false;
                if (weight>0) required=std::max(required,static_cast<size_t>(vertex.joints[influence])+2);
            }
        auto texture = resources->GetTexture(device, queue, mesh.texturePath);
        if (!texture || !CreateIndexedMeshBuffer(device, queue, std::as_bytes(std::span(mesh.vertices)), sizeof(MeshVertex),
            mesh.indices, meshBuffer_, vertexView_, indexView_))
        {
            return false;
        }
        resources_ = resources;
        texture_ = std::move(texture);
        indexCount_ = static_cast<UINT>(mesh.indices.size());
        requiredPaletteSize_=required;
        initialized_ = true;
        Log::Info(std::format("Mesh renderer initialized: {} triangles.", indexCount_ / 3));
        return true;
    }

    void MeshRenderer::Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light,
        const std::array<float, 3>& cameraPosition, const UvTransform& uvTransform,const Material* material,std::span<const SkinMatrix> palette) const
    {
        if (!initialized_ || commands == nullptr)
        {
            return;
        }
        if (!palette.empty() && palette.size()<requiredPaletteSize_) { Log::Error("Skin palette does not cover vertex joints."); return; }
        using namespace DirectX;
        const XMMATRIX worldMatrix = XMLoadFloat4x4(&world);
        // Normal cofactors are derived from the world rows in the vertex shader.
        struct TransformConstants
        {
            XMFLOAT4X4 worldViewProjection;
            XMFLOAT4 worldRows[3];
            std::array<float,4> color{1,1,1,1};
        } constants;
        const bool mirrored=XMVectorGetX(XMMatrixDeterminant(worldMatrix))<0;
        XMStoreFloat4x4(&constants.worldViewProjection, worldMatrix * XMLoadFloat4x4(&viewProjection));
        const XMMATRIX transposedWorld = XMMatrixTranspose(worldMatrix);
        for (size_t row = 0; row < 3; ++row)
        {
            XMStoreFloat4(&constants.worldRows[row], transposedWorld.r[row]);
        }
        if (material) constants.color=material->color;
        static_assert(sizeof(constants) == sizeof(float) * 32);
        std::array<float, 26> lightConstants
        {
            light.direction[0], light.direction[1], light.direction[2], std::max(0.0f, light.intensity),
            light.color[0], light.color[1], light.color[2], std::max(0.0f, light.ambientIntensity),
            cameraPosition[0], cameraPosition[1], cameraPosition[2], std::max(1.0f, light.shininess),
            std::max(0.0f, light.specularStrength), light.enabled ? 1.0f : 0.0f,
            std::max(0.0f,light.fog.start), std::max(light.fog.start+0.001f,light.fog.end),
            light.fog.color[0],light.fog.color[1],light.fog.color[2],
            light.fog.enabled ? std::clamp(light.fog.strength,0.0f,1.0f) : 0.0f
        };
        if (light.shadow) std::copy(light.shadow->Constants().begin(),light.shadow->Constants().end(),lightConstants.begin()+20);
        if (material)
        {
            const float roughness=std::clamp(material->roughness,0.04f,1.0f);
            lightConstants[11]=std::clamp(2.0f/(roughness*roughness*roughness*roughness)-2.0f,1.0f,8192.0f);
            lightConstants[12]=0.04f+std::clamp(material->metallic,0.0f,1.0f)*0.96f;
            if (material->physicallyBased)
            {
                // 負値は PBR の知覚的粗さ、正値は従来のハイライト指数です。
                lightConstants[11]=-roughness;
                lightConstants[12]=std::clamp(material->metallic,0.0f,1.0f);
            }
            // lightingEnabled は整数ビット列: 照明、法線画像、Y反転、局所ライト。
            lightConstants[13]=static_cast<float>((light.enabled ? 1 : 0) |
                (material->normalTexture ? 2 : 0) | (material->normalFlipY ? 4 : 0));
        }
        LocalLightView localLights;
        RenderTargetBinding target;
        const bool hdr=RenderTargetBinding::Current(commands,target) && target.format==DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (hdr) lightConstants[13]=static_cast<float>(static_cast<unsigned int>(lightConstants[13])|16U);
        if (!light.localLights.empty())
        {
            localLights=resources_->PrepareLights(commands,light.localLights);
            if (!localLights.resource) { Log::Error("Cannot prepare local lights for the current render frame."); return; }
            lightConstants[13]=static_cast<float>(static_cast<unsigned int>(lightConstants[13])|8U);
        }
        const auto skin=palette.empty() ? SkinPaletteView{} : resources_->PrepareSkin(commands,palette);
        if (!palette.empty() && !skin.resource) { Log::Error("Cannot prepare mesh skin palette."); return; }
        auto* heap=Bindings(light.shadow,material ? material->texture : nullptr,material ? material->normalTexture : nullptr,localLights,skin);
        if (!heap) { Log::Error("Cannot allocate mesh texture/shadow bindings."); return; }
        // Transform/tint 32 + lighting 26 + compact UV 5 + SRV table 1 = 64 DWORD.
        commands->SetPipelineState(resources_->GetPipelineState(mirrored,material && (material->transparent || material->color[3]<1),hdr));
        commands->SetGraphicsRootSignature(resources_->GetRootSignature());
        commands->SetGraphicsRoot32BitConstants(0, 32, &constants, 0);
        commands->SetGraphicsRoot32BitConstants(2, 26, lightConstants.data(), 0);
        const std::array<float,5> uvConstants{uvTransform.scale[0],uvTransform.scale[1],uvTransform.rotation,uvTransform.translation[0],uvTransform.translation[1]};
        commands->SetGraphicsRoot32BitConstants(3, 5, uvConstants.data(), 0);
        ID3D12DescriptorHeap* heaps[] = { heap };
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0, 1, &vertexView_);
        commands->IASetIndexBuffer(&indexView_);
        commands->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
    }
}
