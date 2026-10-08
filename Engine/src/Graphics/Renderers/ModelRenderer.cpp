#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <Engine/Graphics/Models/ModelLoader.h>
#include <limits>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace Engine
{
    void ModelRenderer::DrawShadow(ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& world,const ShadowMap& shadow) const
    {
        for (size_t index=0;index<meshes_.size();++index)
            meshes_[index]->DrawShadow(commands,world,shadow,rig_ ? std::span<const SkinMatrix>(palettes_[index]) : std::span<const SkinMatrix>{});
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
            try { for (const auto& mesh : rig->meshes) data.push_back(Skeleton::BindMesh(mesh)); }
            catch (const std::exception& exception) { Log::Error(exception.what()); return false; }
        }
        else if (!ModelLoader::Load(modelPath,data)) return false;
        resources_=std::make_shared<MeshResources>();
        if (!resources_->Initialize(device,shaderPath)) { resources_.reset(); return false; }
        device_=device; queue_=queue;
        if (!Rebuild(data)) return false;
        rig_=std::move(rig);
        if (rig_)
        {
            try { PrepareSkinBounds(data); }
            catch (const std::exception& exception) { Log::Error(exception.what()); return false; }
            if (!ApplyPose(Skeleton::Sample(*rig_,"",0,false))) return false;
        }
        return true;
    }
    bool ModelRenderer::Rebuild(const std::vector<MeshData>& data)
    {
        std::vector<std::shared_ptr<MeshRenderer>> loaded;
        std::vector<DirectX::XMFLOAT3> positions;
        for (const auto& mesh : data)
        {
            auto renderer=std::make_shared<MeshRenderer>();
            if (!renderer->Initialize(device_.Get(),queue_.Get(),mesh,resources_)) return false;
            loaded.push_back(std::move(renderer));
            for (const auto index : mesh.indices)
            {
                const auto& point=mesh.vertices.at(index).position; positions.push_back({point[0],point[1],point[2]});
            }
        }
        if (positions.empty()) return false;
        DirectX::BoundingBox bounds; DirectX::BoundingBox::CreateFromPoints(bounds,positions.size(),positions.data(),sizeof(DirectX::XMFLOAT3));
        bounds_=bounds; triangleCount_=positions.size()/3; trianglePositions_=std::move(positions); meshes_=std::move(loaded); return true;
    }
    void ModelRenderer::PrepareSkinBounds(const std::vector<MeshData>& data)
    {
        skinBounds_.clear();
        for (size_t meshIndex=0;meshIndex<data.size();++meshIndex)
        {
            const auto& mesh=data[meshIndex];
            std::vector<std::optional<DirectX::BoundingBox>> groups(rig_->meshes[meshIndex].joints.size()+1);
            for (const auto index : mesh.indices)
            {
                const auto& vertex=mesh.vertices.at(index);
                for (const auto value : vertex.position) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite bind position");
                const DirectX::XMFLOAT3 point{vertex.position[0],vertex.position[1],vertex.position[2]};
                const auto add=[&](size_t group)
                {
                    auto& box=groups.at(group);
                    DirectX::BoundingBox single(point,{0,0,0});
                    if (box) DirectX::BoundingBox::CreateMerged(*box,*box,single);
                    else box=single;
                };
                bool weighted=false;
                for (size_t influence=0;influence<4;++influence)
                    if (vertex.weights[influence]>0) { add(vertex.joints[influence]+1); weighted=true; }
                if (!weighted) add(0);
            }
            skinBounds_.push_back(std::move(groups));
        }
    }
    void ModelRenderer::PreparePickGeometry() const
    {
        if (!pickDirty_) return;
        std::vector<DirectX::XMFLOAT3> positions;
        positions.reserve(triangleCount_*3);
        for (const auto& source : rig_->meshes)
        {
            const auto mesh=Skeleton::Skin(*rig_,source,nodeMatrices_);
            for (const auto index : mesh.indices)
            {
                const auto& point=mesh.vertices.at(index).position;
                positions.push_back({point[0],point[1],point[2]});
            }
        }
        trianglePositions_=std::move(positions); pickDirty_=false;
    }
    std::shared_ptr<ModelRenderer> ModelRenderer::AnimatedCopy(ID3D12Device* device,ID3D12CommandQueue* queue) const
    {
        if (!rig_ || device!=device_.Get() || queue!=queue_.Get()) return {};
        auto result=std::make_shared<ModelRenderer>(); result->rig_=rig_; result->resources_=resources_;
        result->device_=device; result->queue_=queue;
        result->meshes_=meshes_; result->skinBounds_=skinBounds_; result->triangleCount_=triangleCount_;
        if (!result->ApplyPose(Skeleton::Sample(*rig_,"",0,false))) return {};
        return result;
    }
    bool ModelRenderer::ApplyPose(const std::vector<BonePose>& pose)
    {
        if (!rig_) return false;
        try
        {
            auto matrices=Skeleton::Matrices(*rig_,pose);
            std::vector<std::vector<SkinMatrix>> palettes;
            std::vector<DirectX::XMFLOAT3> corners;
            for (size_t index=0;index<rig_->meshes.size();++index)
            {
                auto palette=Skeleton::Palette(*rig_,rig_->meshes[index],matrices);
                for (size_t group=0;group<skinBounds_.at(index).size();++group)
                {
                    const auto& box=skinBounds_[index][group]; if (!box) continue;
                    DirectX::XMFLOAT3 points[8]; box->GetCorners(points);
                    for (auto point : points)
                    {
                        DirectX::XMStoreFloat3(&point,DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&point),DirectX::XMLoadFloat4x4(&palette[group].position)));
                        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) throw std::runtime_error("Nonfinite posed bounds");
                        corners.push_back(point);
                    }
                }
                palettes.push_back(std::move(palette));
            }
            if (corners.empty()) return false;
            DirectX::BoundingBox bounds;
            DirectX::BoundingBox::CreateFromPoints(bounds,corners.size(),corners.data(),sizeof(DirectX::XMFLOAT3));
            for (const float value : {bounds.Center.x,bounds.Center.y,bounds.Center.z,bounds.Extents.x,bounds.Extents.y,bounds.Extents.z})
                if (!std::isfinite(value)) throw std::runtime_error("Overflowed posed bounds");
            // 丸め誤差でウェイト付き頂点が境界の外へ出ないよう、保守的な余白を加えます。
            bounds.Extents.x+=std::max(1.0e-5f,std::abs(bounds.Center.x)*1.0e-6f+bounds.Extents.x*1.0e-6f);
            bounds.Extents.y+=std::max(1.0e-5f,std::abs(bounds.Center.y)*1.0e-6f+bounds.Extents.y*1.0e-6f);
            bounds.Extents.z+=std::max(1.0e-5f,std::abs(bounds.Center.z)*1.0e-6f+bounds.Extents.z*1.0e-6f);
            auto copiedPose=pose;
            palettes_=std::move(palettes); nodeMatrices_=std::move(matrices); pose_=std::move(copiedPose); bounds_=bounds; pickDirty_=true;
            return true;
        }
        catch (const std::exception& exception) { Log::Warning(exception.what()); return false; }
    }

    void ModelRenderer::Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light,
        const std::array<float, 3>& cameraPosition, const UvTransform& uvTransform,const Material* material) const
    {
        for (size_t index=0;index<meshes_.size();++index)
        {
            meshes_[index]->Draw(commands, world, viewProjection, light, cameraPosition, uvTransform,material,
                rig_ ? std::span<const SkinMatrix>(palettes_[index]) : std::span<const SkinMatrix>{});
        }
    }
    bool ModelRenderer::IntersectRay(DirectX::FXMVECTOR origin, DirectX::FXMVECTOR direction,
        float& distance) const
    {
        float broadDistance;
        if (!bounds_.Intersects(origin, direction, broadDistance)) return false;
        try { PreparePickGeometry(); }
        catch (const std::exception& exception) { Log::Warning(exception.what()); return false; }
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
