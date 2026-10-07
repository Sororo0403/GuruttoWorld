#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <Engine/Graphics/Models/ModelLoader.h>
#include <limits>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cctype>

namespace Engine
{
    void ModelRenderer::DrawShadow(ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& world,const ShadowMap& shadow) const
    {
        for (const auto& mesh:meshes_) mesh->DrawShadow(commands,world,shadow);
    }
    bool ModelRenderer::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
        const std::filesystem::path& modelPath, const std::filesystem::path& shaderPath)
    {
        if (!meshes_.empty() || device == nullptr || queue == nullptr)
        {
            return false;
        }
        std::vector<MeshData> data;
        auto extension=modelPath.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        std::shared_ptr<const SkeletonData> rig;
        if (extension==".gltf" || extension==".glb")
        {
            std::string error; rig=Skeleton::Load(modelPath,error);
            if (!rig) { Log::Error(error); return false; }
            const auto matrices=Skeleton::Matrices(*rig,Skeleton::Sample(*rig,"",0,false));
            for (const auto& mesh : rig->meshes) data.push_back(Skeleton::Skin(*rig,mesh,matrices));
        }
        else if (!ModelLoader::Load(modelPath,data)) return false;
        resources_=std::make_shared<MeshResources>();
        if (!resources_->Initialize(device,shaderPath)) { resources_.reset(); return false; }
        device_=device; queue_=queue;
        if (!Rebuild(data)) return false;
        rig_=std::move(rig); if (rig_) pose_=Skeleton::Sample(*rig_,"",0,false);
        return true;
    }
    bool ModelRenderer::Rebuild(const std::vector<MeshData>& data)
    {
        std::vector<std::unique_ptr<MeshRenderer>> loaded;
        std::vector<DirectX::XMFLOAT3> positions;
        for (const auto& mesh : data)
        {
            auto renderer=std::make_unique<MeshRenderer>();
            if (!renderer->Initialize(device_.Get(),queue_.Get(),mesh,resources_)) return false;
            loaded.push_back(std::move(renderer));
            for (const auto index : mesh.indices)
            {
                const auto& point=mesh.vertices.at(index).position; positions.push_back({point[0],point[1],point[2]});
            }
        }
        if (positions.empty()) return false;
        DirectX::BoundingBox bounds; DirectX::BoundingBox::CreateFromPoints(bounds,positions.size(),positions.data(),sizeof(DirectX::XMFLOAT3));
        bounds_=bounds; trianglePositions_=std::move(positions); meshes_=std::move(loaded); return true;
    }
    std::shared_ptr<ModelRenderer> ModelRenderer::AnimatedCopy(ID3D12Device* device,ID3D12CommandQueue* queue) const
    {
        if (!rig_ || device!=device_.Get() || queue!=queue_.Get()) return {};
        auto result=std::make_shared<ModelRenderer>(); result->rig_=rig_; result->resources_=resources_;
        result->device_=device; result->queue_=queue;
        if (!result->ApplyPose(Skeleton::Sample(*rig_,"",0,false))) return {};
        return result;
    }
    bool ModelRenderer::ApplyPose(const std::vector<BonePose>& pose)
    {
        if (!rig_) return false;
        try
        {
            const auto matrices=Skeleton::Matrices(*rig_,pose);
            std::vector<MeshData> data;
            for (const auto& mesh : rig_->meshes) data.push_back(Skeleton::Skin(*rig_,mesh,matrices));
            // Uploads complete on the draw queue before old vertex buffers are released.
            if (!Rebuild(data)) return false;
            pose_=pose; return true;
        }
        catch (const std::exception& exception) { Log::Warning(exception.what()); return false; }
    }

    void ModelRenderer::Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light,
        const std::array<float, 3>& cameraPosition, const UvTransform& uvTransform,const Material* material) const
    {
        for (const auto& mesh : meshes_)
        {
            mesh->Draw(commands, world, viewProjection, light, cameraPosition, uvTransform,material);
        }
    }
    bool ModelRenderer::IntersectRay(DirectX::FXMVECTOR origin, DirectX::FXMVECTOR direction,
        float& distance) const
    {
        float broadDistance;
        if (!bounds_.Intersects(origin, direction, broadDistance)) return false;
        float closest = (std::numeric_limits<float>::max)();
        bool found = false;
        for (size_t index = 0; index + 2 < trianglePositions_.size(); index += 3)
        {
            float hit;
            if (DirectX::TriangleTests::Intersects(origin, direction,
                DirectX::XMLoadFloat3(&trianglePositions_[index]),
                DirectX::XMLoadFloat3(&trianglePositions_[index + 1]),
                DirectX::XMLoadFloat3(&trianglePositions_[index + 2]), hit) && hit < closest)
            {
                closest = hit;
                found = true;
            }
        }
        if (found) distance = closest;
        return found;
    }
}
