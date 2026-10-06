#include <SceneRuntime/SceneWorld.h>
#include <SceneRuntime/SceneTransforms.h>
#include <SceneRuntime/TransformMatrix.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <format>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <cmath>
#include <Engine/Graphics/Renderers/ModelRenderer.h>

namespace
{
    bool HasSelectedAncestor(const SceneRuntime::SceneLayout& layout, std::string parent,
        const std::vector<std::string>& ids)
    {
        while (!parent.empty())
        {
            if (std::find(ids.begin(),ids.end(),parent)!=ids.end()) return true;
            const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),
                [&](const auto& placement) { return placement.id==parent; });
            if (found==layout.objects.end()) return false;
            parent=found->parentId;
        }
        return false;
    }
}

namespace SceneRuntime
{
    bool SceneWorld::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
        const std::filesystem::path& layoutPath, const std::filesystem::path& shaderPath, std::string* diagnostic)
    {
        modelsReady_ = models_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), shaderPath);
        if (!modelsReady_)
        {
            if (diagnostic) *diagnostic = "Scene renderer could not be initialized. Check shaders and restart.";
            return false;
        }
        std::string error;
        const bool loaded = Reload(assetsRoot, layoutPath, error);
        if (diagnostic) *diagnostic = error;
        return loaded;
    }

    bool SceneWorld::Reload(const std::filesystem::path& assetsRoot, const std::filesystem::path& layoutPath,
        std::string& error)
    {
        try { return ReplaceLayout(SceneLayout::Load(layoutPath), assetsRoot, error); }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool SceneWorld::ReplaceLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error)
    {
        if (!modelsReady_)
        {
            error = "Scene renderer is unavailable. Check shaders and restart.";
            return false;
        }
        try
        {
            static_cast<void>(layout.Serialize());
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(layout,layout.transformSpace,matrices,error)) throw std::runtime_error(error);
            std::vector<Engine::Object3D> objects;
            objects.reserve(layout.objects.size());
            for (const auto& placement : layout.objects)
            {
                Engine::Object3D object;
                const auto model = models_.Load(assetsRoot / placement.model);
                if (!model) throw std::runtime_error("Model could not be loaded: " + placement.id);
                object.SetModel(model);
                if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
                    throw std::runtime_error("Invalid transform: " + placement.id);
                if (!object.SetWorldMatrix(matrices[objects.size()])) throw std::runtime_error("Invalid world matrix");
                objects.push_back(std::move(object));
            }
            layout_ = std::move(layout);
            objects_ = std::move(objects);
            error.clear();
            return true;
        }
        catch (const std::exception& errorException)
        {
            error = std::string("Scene could not be loaded: ") + errorException.what();
            Engine::Log::Error(error);
            return false;
        }
    }

    void SceneWorld::Draw(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
        const Engine::DirectionalLight& light) const
    {
        for (const auto& object : objects_) object.Draw(commands, camera, light);
    }

    bool SceneWorld::CommitTransforms(SceneLayout candidate)
    {
        std::vector<DirectX::XMFLOAT4X4> matrices;
        std::string error;
        if (!SceneTransforms::Resolve(candidate,candidate.transformSpace,matrices,error)) return false;
        auto objects=objects_;
        if (objects.size()!=matrices.size()) return false;
        for (size_t index=0;index<objects.size();++index)
        {
            const auto& placement=candidate.objects[index];
            if (!objects[index].SetTransform(placement.position,placement.rotation,placement.scale) ||
                !objects[index].SetWorldMatrix(matrices[index])) return false;
        }
        for (size_t index=0;index<layout_.objects.size();++index)
        {
            auto& target=layout_.objects[index];
            const auto& source=candidate.objects[index];
            target.position=source.position; target.rotation=source.rotation; target.scale=source.scale;
            target.parentId=std::move(candidate.objects[index].parentId);
        }
        layout_.transformSpace=candidate.transformSpace;
        objects_=std::move(objects);
        return true;
    }

    bool SceneWorld::EnableParentTransforms(std::string& error)
    {
        SceneLayout candidate;
        if (!SceneTransforms::ConvertToLocal(layout_,candidate,error)) return false;
        if (!CommitTransforms(std::move(candidate))) { error="Could not apply converted transforms"; return false; }
        error.clear();
        return true;
    }

    bool SceneWorld::SetParent(std::string_view id, std::string parentId, std::string& error)
    {
        try
        {
            auto candidate=layout_;
            const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
                [&](const auto& object) { return object.id==id; });
            if (found==candidate.objects.end()) throw std::runtime_error("Object no longer exists");
            DirectX::XMFLOAT4X4 matrix;
            if (!WorldMatrix(id,matrix)) throw std::runtime_error("World matrix is unavailable");
            found->parentId=std::move(parentId);
            static_cast<void>(candidate.Serialize());
            if (candidate.transformSpace==TransformSpace::Local)
            {
                DirectX::XMFLOAT4X4 parent;
                if (!found->parentId.empty() && (!WorldMatrix(found->parentId,parent) ||
                    !SceneTransforms::WorldToLocal(matrix,parent,matrix))) throw std::runtime_error("Invalid parent transform");
                auto reference=*found;
                if (!TransformMatrix::Read(matrix,reference,*found))
                    throw std::runtime_error("Parent change requires shear that cannot be stored as position, rotation and scale");
            }
            if (!CommitTransforms(std::move(candidate))) throw std::runtime_error("Invalid inherited transform");
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::RenameObject(std::string_view id, std::string name)
    {
        if (name.find_first_not_of(" \t\r\n")==std::string::npos || name.find('\0')!=std::string::npos) return false;
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
            [id](const ScenePlacement& object) { return object.id==id; });
        if (found==layout_.objects.end()) return false;
        found->name=std::move(name);
        return true;
    }

    bool SceneWorld::SetTransform(std::string_view id, const std::array<float, 3>& position,
        const std::array<float, 3>& rotation, const std::array<float, 3>& scale)
    {
        auto candidate=layout_;
        const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
            [&](const auto& placement) { return placement.id==id; });
        if (found==candidate.objects.end()) return false;
        found->position=position;
        found->rotation=rotation;
        found->scale=scale;
        return CommitTransforms(std::move(candidate));
    }

    bool SceneWorld::TranslateObjects(const std::vector<std::string>& ids, const std::array<float,3>& delta)
    {
        if (ids.empty() || !std::all_of(delta.begin(),delta.end(),[](float value) { return std::isfinite(value); })) return false;
        auto candidate=layout_;
        std::vector<std::string> seen;
        for (const auto& id : ids)
        {
            const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
                [&](const auto& placement) { return placement.id==id; });
            if (found==candidate.objects.end() || std::find(seen.begin(),seen.end(),id)!=seen.end()) return false;
            seen.push_back(id);
            if (candidate.transformSpace==TransformSpace::Local)
            {
                if (HasSelectedAncestor(layout_,found->parentId,ids)) continue;
            }
            DirectX::XMFLOAT4X4 matrix;
            ScenePlacement translated;
            if (!WorldMatrix(id,matrix)) return false;
            matrix._41+=delta[0]; matrix._42+=delta[1]; matrix._43+=delta[2];
            if (!ToPlacement(id,matrix,translated)) return false;
            found->position=translated.position;
        }
        return CommitTransforms(std::move(candidate));
    }

    std::string SceneWorld::NewId()
    {
        for (;;)
        {
            const auto id = "object-" + std::to_string(nextObjectId_++);
            if (std::none_of(layout_.objects.begin(), layout_.objects.end(),
                [&](const auto& placement) { return placement.id == id; })) return id;
        }
    }

    void SceneWorld::Append(ScenePlacement placement, Engine::Object3D object)
    {
        layout_.objects.reserve(layout_.objects.size() + 1);
        objects_.reserve(objects_.size() + 1);
        layout_.objects.push_back(std::move(placement));
        objects_.push_back(std::move(object));
    }

    bool SceneWorld::AddObject(ScenePlacement placement, const std::filesystem::path& assetsRoot,
        std::string& createdId, std::string& error)
    {
        createdId.clear();
        try
        {
            if (!modelsReady_) throw std::runtime_error("Scene renderer is unavailable");
            if (placement.id.empty()) placement.id = NewId();
            if (std::any_of(layout_.objects.begin(), layout_.objects.end(),
                [&](const auto& existing) { return existing.id == placement.id; }))
                throw std::runtime_error("Object ID already exists");
            if (placement.name.empty()) placement.name = placement.model.stem().string();
            auto validation=layout_;
            validation.objects.push_back(placement);
            static_cast<void>(validation.Serialize());
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(validation,validation.transformSpace,matrices,error)) throw std::runtime_error(error);
            Engine::Object3D object;
            if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
                throw std::runtime_error("Invalid transform");
            if (!object.SetWorldMatrix(matrices.back())) throw std::runtime_error("Invalid world matrix");
            const auto model = models_.Load(assetsRoot / placement.model);
            if (!model) throw std::runtime_error("Model could not be loaded");
            object.SetModel(model);
            const auto id = placement.id;
            Append(std::move(placement), std::move(object));
            createdId = id;
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool SceneWorld::DuplicateObject(std::string_view id, const std::array<float, 3>& offset,
        std::string& createdId, std::string& error)
    {
        createdId.clear();
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
            [id](const auto& placement) { return placement.id == id; });
        if (found == layout_.objects.end()) { error = "Object no longer exists"; return false; }
        auto placement = *found;
        auto object = objects_[static_cast<size_t>(found - layout_.objects.begin())];
        DirectX::XMFLOAT4X4 matrix;
        if (!WorldMatrix(id,matrix)) { error="World matrix is unavailable"; return false; }
        matrix._41+=offset[0]; matrix._42+=offset[1]; matrix._43+=offset[2];
        if (!ToPlacement(id,matrix,placement)) { error="Invalid duplicate offset"; return false; }
        placement.id = NewId();
        placement.name += " copy";
        if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
        {
            error = "Invalid duplicate offset";
            return false;
        }
        if (!object.SetWorldMatrix(matrix)) { error="Invalid duplicate matrix"; return false; }
        const auto newId = placement.id;
        Append(std::move(placement), std::move(object));
        createdId = newId;
        error.clear();
        return true;
    }

    bool SceneWorld::RemoveObject(std::string_view id)
    {
        auto candidate=layout_;
        auto objects=objects_;
        const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
            [&](const auto& placement) { return placement.id==id; });
        if (found==candidate.objects.end()) return false;
        const auto index=static_cast<size_t>(found-candidate.objects.begin());
        const auto removedId=found->id;
        for (auto& placement : candidate.objects)
        {
            if (placement.parentId!=removedId) continue;
            placement.parentId.clear();
            if (candidate.transformSpace==TransformSpace::Local)
            {
                DirectX::XMFLOAT4X4 matrix;
                auto reference=placement;
                if (!WorldMatrix(placement.id,matrix) || !TransformMatrix::Read(matrix,reference,placement)) return false;
            }
        }
        candidate.objects.erase(found);
        objects.erase(objects.begin()+static_cast<std::ptrdiff_t>(index));
        std::vector<DirectX::XMFLOAT4X4> matrices;
        std::string error;
        if (!SceneTransforms::Resolve(candidate,candidate.transformSpace,matrices,error)) return false;
        for (size_t item=0;item<objects.size();++item)
            if (!objects[item].SetWorldMatrix(matrices[item])) return false;
        layout_=std::move(candidate);
        objects_=std::move(objects);
        return true;
    }
    std::optional<std::string> SceneWorld::PickRay(const std::array<float, 3>& origin,
        const std::array<float, 3>& direction, float maxDistance) const
    {
        const auto finite = [](const auto& vector) {
            return std::all_of(vector.begin(), vector.end(), [](float value) { return std::isfinite(value); });
        };
        if (!finite(origin) || !finite(direction) || !std::isfinite(maxDistance) || maxDistance <= 0) return {};
        using namespace DirectX;
        const auto start = XMVectorSet(origin[0], origin[1], origin[2], 1);
        auto ray = XMVectorSet(direction[0], direction[1], direction[2], 0);
        const float length = XMVectorGetX(XMVector3Length(ray));
        if (!std::isfinite(length) || length <= 0) return {};
        ray /= length;
        float closest = maxDistance;
        std::optional<std::string> selected;
        for (size_t index = 0; index < objects_.size(); ++index)
        {
            const auto& object = objects_[index];
            const auto inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&object.GetWorldMatrix()));
            const auto localStart = XMVector3TransformCoord(start, inverse);
            auto localRay = XMVector3TransformNormal(ray, inverse);
            const float factor = XMVectorGetX(XMVector3Length(localRay));
            if (!std::isfinite(factor) || factor <= 0) continue;
            localRay /= factor;
            float hit;
            if (object.GetModel()->IntersectRay(localStart, localRay, hit) && hit / factor < closest)
            {
                closest = hit / factor;
                selected = layout_.objects[index].id;
            }
        }
        return selected;
    }

    bool SceneWorld::WorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
            [&](const auto& placement) { return placement.id==id; });
        if (found==layout_.objects.end()) return false;
        matrix=objects_[static_cast<size_t>(found-layout_.objects.begin())].GetWorldMatrix();
        return true;
    }

    bool SceneWorld::ToPlacement(std::string_view id, const DirectX::XMFLOAT4X4& world, ScenePlacement& placement) const
    {
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
            [&](const auto& value) { return value.id==id; });
        if (found==layout_.objects.end()) return false;
        auto matrix=world;
        if (layout_.transformSpace==TransformSpace::Local && !found->parentId.empty())
        {
            DirectX::XMFLOAT4X4 parent;
            if (!WorldMatrix(found->parentId,parent) || !SceneTransforms::WorldToLocal(world,parent,matrix)) return false;
        }
        return TransformMatrix::Read(matrix,*found,placement);
    }

    bool SceneWorld::WorldBounds(std::string_view id, std::array<std::array<float, 3>, 8>& corners) const
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
            [id](const auto& placement) { return placement.id == id; });
        if (found == layout_.objects.end()) return false;
        const auto& object = objects_[static_cast<size_t>(found - layout_.objects.begin())];
        DirectX::XMFLOAT3 localCorners[8];
        object.GetModel()->Bounds().GetCorners(localCorners);
        const auto matrix = DirectX::XMLoadFloat4x4(&object.GetWorldMatrix());
        for (size_t i = 0; i < 8; ++i)
        {
            DirectX::XMFLOAT3 position;
            DirectX::XMStoreFloat3(&position, DirectX::XMVector3TransformCoord(
                DirectX::XMLoadFloat3(&localCorners[i]), matrix));
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
            corners[i] = { position.x, position.y, position.z };
        }
        return true;
    }
}
