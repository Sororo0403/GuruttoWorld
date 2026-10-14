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
static size_t RequiredPaletteSize(const MeshData &mesh)
{
    size_t required = 1;
    for (const auto &vertex : mesh.vertices)
        for (size_t influence = 0; influence < 4; ++influence)
        {
            const float weight = vertex.weights[influence];
            if (!std::isfinite(weight) || weight < 0 || (weight > 0 && vertex.joints[influence] >= 256)) return 0;
            if (weight > 0) required = std::max(required, static_cast<size_t>(vertex.joints[influence]) + 2);
        }
    return required;
}

static std::array<float, 26> LightConstants(const DirectionalLight &light, const std::array<float, 3> &cameraPosition, const Material *material)
{
    std::array<float, 26> lightConstants
        {
        light.direction[0], light.direction[1], light.direction[2], std::max(0.0f, light.intensity),
        light.color[0], light.color[1], light.color[2], std::max(0.0f, light.ambientIntensity),
        cameraPosition[0], cameraPosition[1], cameraPosition[2], std::max(1.0f, light.shininess),
        std::max(0.0f, light.specularStrength), light.enabled ? 1.0f : 0.0f,
        std::max(0.0f, light.fog.start), std::max(light.fog.start + 0.001f, light.fog.end),
        light.fog.color[0], light.fog.color[1], light.fog.color[2],
        light.fog.enabled ? std::clamp(light.fog.strength, 0.0f, 1.0f) : 0.0f
        };
    if (light.shadow) std::copy(light.shadow->Constants().begin(), light.shadow->Constants().end(), lightConstants.begin() + 20);
    if (material)
    {
        const float roughness = std::clamp(material->roughness, 0.04f, 1.0f);
        lightConstants[11] = std::clamp(2.0f / (roughness * roughness * roughness * roughness) - 2.0f, 1.0f, 8192.0f);
        lightConstants[12] = 0.04f + std::clamp(material->metallic, 0.0f, 1.0f) * 0.96f;
        if (material->physicallyBased)
        {
            // 負値は PBR の知覚的粗さ、正値は従来のハイライト指数です。
            lightConstants[11] = -roughness;
            lightConstants[12] = std::clamp(material->metallic, 0.0f, 1.0f);
        }
        // lightingEnabled は整数ビット列: 照明、法線画像、Y反転、局所ライト。
        lightConstants[13] = static_cast<float>((light.enabled ? 1 : 0) |
                                                (material->normalTexture ? 2 : 0) | (material->normalFlipY ? 4 : 0) |
                                                (static_cast<unsigned int>(std::lround(std::clamp(material->effectAmount,0.0f,1.0f)*1023))<<12));
    }
    return lightConstants;
}
bool MeshRenderer::PrepareLocalLighting(ID3D12GraphicsCommandList *commands, const DirectionalLight &light, const Material *material, std::array<float, 26> &lightConstants, LocalLightView &localLights) const
{
    const bool environment = material && material->environmentTexture && material->environmentIntensity > 0;
    if (!light.localLights.empty() || environment)
    {
        localLights = resources_->PrepareLights(commands, light.localLights, light.localShadow,
                                                environment ? std::array<float, 2>{material->environmentIntensity, static_cast<float>(material->environmentTexture->GetMipLevels())} : std::array<float, 2>{});
        if (!localLights.resource) { Log::Error("Cannot prepare local lights for the current render frame."); return false; }
        lightConstants[13] = static_cast<float>(static_cast<unsigned int>(lightConstants[13]) | 8U);
    }
    if (environment) lightConstants[13] = static_cast<float>(static_cast<unsigned int>(lightConstants[13]) | 64U);
    if (material && material->lightmap) lightConstants[13] = static_cast<float>(static_cast<unsigned int>(lightConstants[13]) | 128U);
    return true;
}
bool MeshRenderer::PrepareDeformation(ID3D12GraphicsCommandList *commands, const MeshDeformation &deformation, const DirectX::XMFLOAT4X4 &viewProjection, bool mirrored, SkinPaletteView &instanceView) const
{
    using namespace DirectX;
    const auto &[palette, instances] = deformation;
    if (!instances.empty())
    {
        if (!palette.empty() || instances.size() > SkinPaletteBuffer::Capacity) return false;
        std::vector<SkinMatrix> matrices; matrices.reserve(instances.size());
        for (const auto &instance : instances)
        {
            const auto matrix = XMLoadFloat4x4(&instance);
            if ((XMVectorGetX(XMMatrixDeterminant(matrix)) < 0) != mirrored) return false;
            SkinMatrix record; XMStoreFloat4x4(&record.position, matrix * XMLoadFloat4x4(&viewProjection));
            record.normal = instance; matrices.push_back(record);
        }
        instanceView = resources_->PrepareInstances(commands, matrices);
        if (!instanceView.resource) return false;
    }
    return true;
}
    ID3D12DescriptorHeap* MeshRenderer::Bindings(const ShadowMap* shadow,const std::shared_ptr<const Texture2D>& overrideTexture,
        const std::shared_ptr<const Texture2D>& normal,const LocalLightView& lights,const SkinPaletteView& palette,const SkinPaletteView& instances,const ShadowMap* localShadow,
        const std::array<std::shared_ptr<const Texture2D>,2>& surfaceImages) const
    {
        const auto& [environment,lightmap]=surfaceImages;
        const std::shared_ptr<const Texture2D> texture=overrideTexture ? overrideTexture : texture_;
        const auto key=std::tuple{shadow ? shadow->Resource() : nullptr,texture.get(),normal.get(),lights.resource,palette.resource,instances.resource,localShadow ? localShadow->Resource() : nullptr,environment.get(),lightmap.get()};
        if (const auto found=bindings_.find(key);found!=bindings_.end()) return found->second.heap.Get();
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(meshBuffer_->GetDevice(IID_PPV_ARGS(&device)))) return nullptr;
        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        description.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; description.NumDescriptors=9;
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
        handle.ptr+=device->GetDescriptorHandleIncrementSize(description.Type);
        if (instances.resource) device->CopyDescriptorsSimple(1,handle,instances.srv,description.Type);
        else {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Buffer.NumElements=1; view.Buffer.StructureByteStride=sizeof(SkinMatrix);
            device->CreateShaderResourceView(nullptr,&view,handle);
        }
        handle.ptr+=device->GetDescriptorHandleIncrementSize(description.Type);
        if (localShadow) device->CopyDescriptorsSimple(1,handle,localShadow->View(),description.Type);
        else {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY; view.Format=DXGI_FORMAT_R32_FLOAT;
            view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Texture2DArray.MipLevels=1; view.Texture2DArray.ArraySize=ShadowMap::LocalFaces;
            device->CreateShaderResourceView(nullptr,&view,handle);
        }
        for (const auto& image : {environment,lightmap})
        {
            handle.ptr+=device->GetDescriptorHandleIncrementSize(description.Type);
            if (image) device->CopyDescriptorsSimple(1,handle,image->GetShaderResourceView(),description.Type);
            else {
                D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
                view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels=1;
                device->CreateShaderResourceView(nullptr,&view,handle);
            }
        }
        // 各組み合わせを保持し、実行中のフレームが参照する SRV を書き換えません。
        return bindings_.emplace(key,Binding{std::move(heap),texture,normal,environment,lightmap}).first->second.heap.Get();
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
        const auto required=RequiredPaletteSize(mesh);
        if(!required) return false;
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

    MeshRenderer::TransformConstants MeshRenderer::MakeTransforms(DirectX::FXMMATRIX worldMatrix, const DirectX::XMFLOAT4X4 &viewProjection, const Material *material)
    {
        using namespace DirectX;
        // Normal cofactors are derived from the world rows in the vertex shader.
        TransformConstants constants;
        XMStoreFloat4x4(&constants.worldViewProjection, worldMatrix * XMLoadFloat4x4(&viewProjection));
        const XMMATRIX transposedWorld = XMMatrixTranspose(worldMatrix);
        for (size_t row = 0; row < 3; ++row)
        {
            XMStoreFloat4(&constants.worldRows[row], transposedWorld.r[row]);
        }
        if (material) constants.color = material->color;
        static_assert(sizeof(constants) == sizeof(float) * 32);
        return constants;
    }
    void MeshRenderer::BindDraw(ID3D12GraphicsCommandList *commands, const DrawBindings &bindings) const
    {
        const auto &[mirrored, hdr, material, transform, lighting, uv, heap] = bindings;
        const auto &constants = *transform; const auto &lightConstants = *lighting; const auto &uvTransform = *uv;
        // Transform/tint 32 + lighting 26 + compact UV 5 + SRV table 1 = 64 DWORD.
        const auto &pipeline = material && material->shaderResources ? material->shaderResources : resources_;
        commands->SetPipelineState(pipeline->GetPipelineState(mirrored, material && (material->transparent || material->color[3] < 1), hdr));
        commands->SetGraphicsRootSignature(pipeline->GetRootSignature());
        commands->SetGraphicsRoot32BitConstants(0, 32, &constants, 0);
        commands->SetGraphicsRoot32BitConstants(2, 26, lightConstants.data(), 0);
        const std::array<float, 5> uvConstants{uvTransform.scale[0], uvTransform.scale[1], uvTransform.rotation, uvTransform.translation[0], uvTransform.translation[1]};
        commands->SetGraphicsRoot32BitConstants(3, 5, uvConstants.data(), 0);
        ID3D12DescriptorHeap *heaps[] = {heap};
        commands->SetDescriptorHeaps(1, heaps);
        commands->SetGraphicsRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0, 1, &vertexView_);
        commands->IASetIndexBuffer(&indexView_);
    }
    ID3D12DescriptorHeap *MeshRenderer::SurfaceBindings(const DirectionalLight &light, const Material *material, const LocalLightView &localLights, const SkinPaletteView &skin, const SkinPaletteView &instanceView) const
    {
        return Bindings(light.shadow, material ? material->texture : nullptr, material ? material->normalTexture : nullptr, localLights, skin, instanceView, light.localShadow,
                        {material ? material->environmentTexture : nullptr, material ? material->lightmap : nullptr});
    }
    void MeshRenderer::Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light,
        const std::array<float, 3>& cameraPosition, const UvTransform& uvTransform,const Material* material,const MeshDeformation& deformation) const
    {
        const auto& [palette,instances]=deformation;
        if (!initialized_ || commands == nullptr)
        {
            return;
        }
        if (!palette.empty() && palette.size()<requiredPaletteSize_) { Log::Error("Skin palette does not cover vertex joints."); return; }
        using namespace DirectX;
        const XMMATRIX worldMatrix = XMLoadFloat4x4(&world);
        const bool mirrored=XMVectorGetX(XMMatrixDeterminant(worldMatrix))<0;
        const auto constants=MakeTransforms(worldMatrix,viewProjection,material);
        auto lightConstants=LightConstants(light,cameraPosition,material);
        LocalLightView localLights;
        RenderTargetBinding target;
        const bool hdr=RenderTargetBinding::Current(commands,target) && target.format==DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (hdr) lightConstants[13]=static_cast<float>(static_cast<unsigned int>(lightConstants[13])|16U);
        if(!PrepareLocalLighting(commands,light,material,lightConstants,localLights)) return;
        const auto skin=palette.empty() ? SkinPaletteView{} : resources_->PrepareSkin(commands,palette);
        if (!palette.empty() && !skin.resource) { Log::Error("Cannot prepare mesh skin palette."); return; }
        SkinPaletteView instanceView;
        if(!PrepareDeformation(commands,deformation,viewProjection,mirrored,instanceView)) return;
        if(!instances.empty()) lightConstants[13]=static_cast<float>(static_cast<unsigned int>(lightConstants[13])|32U);
        auto* heap=SurfaceBindings(light,material,localLights,skin,instanceView);
        if (!heap) { Log::Error("Cannot allocate mesh texture/shadow bindings."); return; }
        BindDraw(commands,{mirrored,hdr,material,&constants,&lightConstants,&uvTransform,heap});
        commands->DrawIndexedInstanced(indexCount_, instances.empty() ? 1U : static_cast<UINT>(instances.size()), 0, 0, 0);
    }
}
