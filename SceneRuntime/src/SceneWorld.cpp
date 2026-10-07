#include <SceneRuntime/SceneWorld.h>
#include <SceneRuntime/SceneTransforms.h>
#include <SceneRuntime/SceneUi.h>
#include <SceneRuntime/Prefab.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <format>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <unordered_map>
#include <Engine/Graphics/Renderers/ModelRenderer.h>

namespace
{
    bool HasTransformAnimation(const SceneRuntime::ScenePlacement& placement)
    {
        return placement.animation && placement.animation->enabled &&
            std::any_of(placement.animation->tracks.begin(),placement.animation->tracks.end(),[](const auto& track) {
                return track.property=="position" || track.property=="rotation";
            });
    }
    bool ApplyTransformAnimation(SceneRuntime::ScenePlacement& placement,const std::map<std::string,float>& clocks)
    {
        if (!HasTransformAnimation(placement)) return false;
        const auto position=placement.position,rotation=placement.rotation;
        for (const auto& track:placement.animation->tracks)
        {
            if (track.property!="position" && track.property!="rotation") continue;
            const auto clock=clocks.find(track.clock);
            if (clock==clocks.end() || clock->second<track.delay) continue;
            const auto value=SceneRuntime::Animation::Sample(track,clocks);
            if (!value) continue;
            auto& target=track.property=="position" ? placement.position : placement.rotation;
            target={(*value)[0],(*value)[1],(*value)[2]};
        }
        return position!=placement.position || rotation!=placement.rotation;
    }
    bool IsRotation(const DirectX::XMFLOAT4X4& rotation)
    {
        if (!SceneRuntime::SceneTransforms::IsUsable(rotation)) return false;
        const auto matrix=DirectX::XMLoadFloat4x4(&rotation);
        DirectX::XMFLOAT4X4 product,identity;
        DirectX::XMStoreFloat4x4(&product,matrix*DirectX::XMMatrixTranspose(matrix));
        DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
        return DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(matrix))>0 &&
            SceneRuntime::SceneTransforms::Matches(product,identity);
    }

    bool IsIdentity(const DirectX::XMFLOAT4X4& matrix)
    {
        for (size_t row=0;row<4;++row)
            for (size_t column=0;column<4;++column)
                if (matrix.m[row][column]!=(row==column ? 1.0f : 0.0f)) return false;
        return true;
    }

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
        if (!InitializeModels(renderer,shaderPath,diagnostic)) return false;
        std::string error;
        const bool loaded = Reload(assetsRoot, layoutPath, error);
        if (diagnostic) *diagnostic = error;
        return loaded;
    }

    bool SceneWorld::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
        SceneLayout layout, const std::filesystem::path& shaderPath, std::string* diagnostic)
    {
        if (!InitializeModels(renderer,shaderPath,diagnostic)) return false;
        std::string error;
        bool loaded=false;
        try { Prefab::Refresh(layout,assetsRoot); loaded=ReplaceLayout(std::move(layout),assetsRoot,error); }
        catch (const std::exception& exception) { error=exception.what(); }
        if (diagnostic) *diagnostic=error;
        return loaded;
    }

    bool SceneWorld::InitializeModels(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& shaderPath,
        std::string* diagnostic)
    {
        modelsReady_ = models_.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), shaderPath) &&
            shadow_.Initialize(renderer.GetDevice(),shaderPath);
        if (!modelsReady_)
        {
            if (diagnostic) *diagnostic = "Scene renderer could not be initialized. Check shaders and restart.";
            return false;
        }
        return true;
    }
    bool SceneWorld::Animate(const std::map<std::string, float>& clocks)
    {
        if (std::none_of(layout_.objects.begin(),layout_.objects.end(),HasTransformAnimation)) return true;
        auto candidate=layout_;
        bool changed=false;
        for (auto& placement:candidate.objects) changed=ApplyTransformAnimation(placement,clocks) || changed;
        return !changed || CommitTransforms(std::move(candidate));
    }

    bool SceneWorld::Reload(const std::filesystem::path& assetsRoot, const std::filesystem::path& layoutPath,
        std::string& error)
    {
        try { auto layout=SceneLayout::Load(layoutPath); Prefab::Refresh(layout,assetsRoot); return ReplaceLayout(std::move(layout), assetsRoot, error); }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool SceneWorld::ReloadAssets(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& assetsRoot,
        const std::filesystem::path& shaderPath, std::string& error)
    {
        SceneWorld candidate;
        if (!candidate.InitializeModels(renderer,shaderPath,&error) || !candidate.ReplaceLayout(layout_,assetsRoot,error)) return false;
        models_.Swap(candidate.models_);
        objects_.swap(candidate.objects_);
        std::swap(shadow_,candidate.shadow_);
        error.clear();
        return true;
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
            if (!SceneTransforms::Resolve(layout,matrices,error)) throw std::runtime_error(error);
            std::vector<Engine::Object3D> objects;
            objects.reserve(layout.objects.size());
            for (const auto& placement : layout.objects)
            {
                Engine::Object3D object;
                if (placement.meshRenderer)
                {
                    const auto model = models_.Load(assetsRoot / placement.Model());
                    if (!model) throw std::runtime_error("Model could not be loaded: " + placement.id);
                    object.SetModel(model);
                }
                if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
                    throw std::runtime_error("Invalid transform: " + placement.id);
                if (!object.SetWorldMatrix(matrices[objects.size()])) throw std::runtime_error("Invalid world matrix");
                objects.push_back(std::move(object));
            }
            scripts_.Stop(layout_);
            layout_ = std::move(layout);
            physics_.clear();
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
        const Engine::DirectionalLight& light, const UiState* state) const
    {
        const auto defaults=state ? UiState{} : SceneUi::Defaults(layout_);
        const auto& values=state ? *state : defaults;
        const auto visible=[&](size_t index) {
            const auto& mesh=layout_.objects[index].meshRenderer;
            return mesh && mesh->enabled && values.Matches(mesh->visibleWhen);
        };
        auto lighting=light;
        lighting.shadow=nullptr;
        if (light.enabled && light.shadowsEnabled && shadow_.Begin(commands,camera,light))
        {
            for (size_t index=0;index<objects_.size();++index)
                if (visible(index) && objects_[index].GetModel())
                    objects_[index].GetModel()->DrawShadow(commands,objects_[index].GetWorldMatrix(),shadow_);
            shadow_.End(commands);
            lighting.shadow=&shadow_;
        }
        for (size_t index=0;index<objects_.size();++index)
            if (visible(index))
                objects_[index].Draw(commands,camera,lighting);
    }

    bool SceneWorld::PrepareTransforms(const SceneLayout& layout, std::vector<Engine::Object3D>& objects, std::string& error)
    {
        std::vector<DirectX::XMFLOAT4X4> matrices;
        if (!SceneTransforms::Resolve(layout,matrices,error)) return false;
        if (objects.size()!=matrices.size()) { error="Placement and draw object counts differ"; return false; }
        for (size_t index=0;index<objects.size();++index)
        {
            const auto& placement=layout.objects[index];
            if (!objects[index].SetTransform(placement.position,placement.rotation,placement.scale) ||
                !objects[index].SetWorldMatrix(matrices[index]))
            { error=placement.id+": invalid inherited transform"; return false; }
        }
        error.clear();
        return true;
    }

    bool SceneWorld::ReparentPlacement(ScenePlacement& placement, std::string parentId, std::string& error) const
    {
        DirectX::XMFLOAT4X4 matrix;
        if (!WorldMatrix(placement.id,matrix)) { error=placement.id+": object no longer exists"; return false; }
        if (!parentId.empty())
        {
            DirectX::XMFLOAT4X4 parent;
            if (!WorldMatrix(parentId,parent) || !SceneTransforms::WorldToLocal(matrix,parent,matrix))
            { error=placement.id+": invalid parent transform"; return false; }
        }
        auto candidate=placement;
        if (!SceneTransforms::ReadTransform(matrix,placement,candidate))
        { error=placement.id+": preserving world placement requires shear that cannot be stored as SRT"; return false; }
        candidate.parentId=std::move(parentId);
        placement=std::move(candidate);
        error.clear();
        return true;
    }

    bool SceneWorld::TranslatePlacement(ScenePlacement& placement, const std::array<float,3>& delta) const
    {
        if (!std::all_of(delta.begin(),delta.end(),[](float value) { return std::isfinite(value); })) return false;
        auto movement=DirectX::XMVectorSet(delta[0],delta[1],delta[2],0);
        if (!placement.parentId.empty())
        {
            DirectX::XMFLOAT4X4 parent;
            if (!WorldMatrix(placement.parentId,parent)) return false;
            movement=DirectX::XMVector3TransformNormal(movement,
                DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&parent)));
        }
        DirectX::XMFLOAT3 local;
        DirectX::XMStoreFloat3(&local,movement);
        placement.position[0]+=local.x; placement.position[1]+=local.y; placement.position[2]+=local.z;
        DirectX::XMFLOAT4X4 matrix;
        return SceneTransforms::Compose(placement,matrix);
    }

    bool SceneWorld::CommitTransforms(SceneLayout candidate)
    {
        auto objects=objects_;
        std::string error;
        if (!PrepareTransforms(candidate,objects,error)) return false;
        for (size_t index=0;index<layout_.objects.size();++index)
        {
            auto& target=layout_.objects[index];
            const auto& source=candidate.objects[index];
            target.position=source.position; target.rotation=source.rotation; target.scale=source.scale;
            target.parentId=std::move(candidate.objects[index].parentId);
        }
        objects_=std::move(objects);
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
            if (found->parentId==parentId) { error.clear(); return true; }
            auto validation=candidate;
            validation.objects[static_cast<size_t>(found-candidate.objects.begin())].parentId=parentId;
            static_cast<void>(validation.Serialize());
            if (!ReparentPlacement(*found,std::move(parentId),error)) return false;
            if (!CommitTransforms(std::move(candidate))) throw std::runtime_error("Invalid inherited transform");
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::SetSettings(const SceneSettings& settings, std::string& error)
    {
        try
        {
            auto candidate=layout_;
            candidate.settings=settings;
            static_cast<void>(candidate.Serialize());
            layout_.settings=settings;
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::SetComponents(std::string_view id, const ScenePlacement& settings,
        const std::filesystem::path& assetsRoot, std::string& error)
    {
        try
        {
            auto candidate=layout_;
            const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
                [&](const auto& placement) { return placement.id==id; });
            if (found==candidate.objects.end()) throw std::runtime_error("Component owner no longer exists");
            found->CopyComponents(settings);
            if (!found->camera && candidate.settings.mainCamera==id) candidate.settings.mainCamera.clear();
            static_cast<void>(candidate.Serialize());
            const auto index=static_cast<size_t>(found-candidate.objects.begin());
            if (found->meshRenderer==layout_.objects[index].meshRenderer)
            {
                layout_.objects[index].CopyComponents(*found);
                layout_.settings=candidate.settings;
                error.clear();
                return true;
            }
            return ReplaceLayout(std::move(candidate),assetsRoot,error);
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }

    bool SceneWorld::MovePlayers(double seconds, float horizontal, float vertical, bool jump)
    {
        auto candidate=layout_;
        auto states=physics_;
        if (!ScenePhysics::Advance(candidate,states,seconds,horizontal,vertical,jump)) return false;
        if (!CommitTransforms(std::move(candidate))) return false;
        physics_=std::move(states);
        return true;
    }

    bool SceneWorld::UpdateComponents(double seconds)
    {
        if (!std::isfinite(seconds) || seconds<=0) return false;
        auto candidate=layout_;
        bool changed=false;
        for (auto& placement : candidate.objects)
        {
            if (!placement.rotator || !placement.rotator->enabled) continue;
            for (size_t axis=0;axis<3;++axis)
            {
                const double speed=placement.rotator->angularVelocity[axis];
                if (speed==0) continue;
                const double elapsed=std::remainder(seconds,360.0/std::abs(speed));
                placement.rotation[axis]=static_cast<float>(std::remainder(static_cast<double>(placement.rotation[axis])+
                    elapsed*speed*DirectX::XM_PI/180.0,static_cast<double>(DirectX::XM_2PI)));
                changed=true;
            }
        }
        auto runtime=scripts_;
        std::string error;
        if (!runtime.Update(candidate,seconds,error)) { Engine::Log::Warning(error); return false; }
        changed=changed || std::any_of(candidate.objects.begin(),candidate.objects.end(),[](const auto& placement) { return !placement.scripts.empty(); });
        if (changed && !CommitTransforms(std::move(candidate))) return false;
        scripts_=std::move(runtime);
        return true;
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

    bool SceneWorld::SetPlacementTransform(std::string_view id, const ScenePlacement& placement)
    {
        auto candidate=layout_;
        const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
            [&](const auto& value) { return value.id==id; });
        if (found==candidate.objects.end()) return false;
        found->position=placement.position;
        found->rotation=placement.rotation;
        found->scale=placement.scale;
        return CommitTransforms(std::move(candidate));
    }

    bool SceneWorld::SetLocalTransform(std::string_view id, const std::array<float,3>& position,
        const std::array<float,3>& rotation, const std::array<float,3>& scale)
    {
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
            [&](const auto& value) { return value.id==id; });
        if (found==layout_.objects.end()) return false;
        auto placement=*found;
        placement.position=position; placement.rotation=rotation; placement.scale=scale;
        return SetPlacementTransform(id,placement);
    }

    bool SceneWorld::SetWorldTransform(std::string_view id, const std::array<float,3>& position,
        const std::array<float,3>& rotation, const std::array<float,3>& scale)
    {
        ScenePlacement placement;
        placement.position=position; placement.rotation=rotation; placement.scale=scale;
        DirectX::XMFLOAT4X4 matrix;
        if (!SceneTransforms::Compose(placement,matrix)) return false;
        return SetWorldTransform(id,matrix);
    }

    bool SceneWorld::SetWorldTransform(std::string_view id, const DirectX::XMFLOAT4X4& matrix)
    {
        ScenePlacement placement;
        if (!LocalTransformFromWorld(id,matrix,placement)) return false;
        return SetPlacementTransform(id,placement);
    }

    bool SceneWorld::TranslateObjectsWorld(const std::vector<std::string>& ids, const std::array<float,3>& delta)
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
            if (HasSelectedAncestor(layout_,found->parentId,ids)) continue;

            if (!TranslatePlacement(*found,delta)) return false;
        }
        return CommitTransforms(std::move(candidate));
    }

    bool SceneWorld::RotateObjectsWorld(const std::vector<std::string>& ids, const std::array<float,3>& pivot,
        const DirectX::XMFLOAT4X4& rotation)
    {
        if (ids.empty() || !IsRotation(rotation) ||
            !std::all_of(pivot.begin(),pivot.end(),[](float value) { return std::isfinite(value); })) return false;
        const auto delta=DirectX::XMMatrixTranslation(-pivot[0],-pivot[1],-pivot[2])*
            DirectX::XMLoadFloat4x4(&rotation)*DirectX::XMMatrixTranslation(pivot[0],pivot[1],pivot[2]);
        DirectX::XMFLOAT4X4 matrix;
        DirectX::XMStoreFloat4x4(&matrix,delta);
        return TransformObjectsWorld(ids,matrix);
    }

    bool SceneWorld::ScaleObjectsWorld(const std::vector<std::string>& ids, const std::array<float,3>& pivot,
        const DirectX::XMFLOAT4X4& axes, const std::array<float,3>& factors)
    {
        if (!IsRotation(axes) || !std::all_of(pivot.begin(),pivot.end(),[](float value) { return std::isfinite(value); }) ||
            !std::all_of(factors.begin(),factors.end(),[](float value) { return std::isfinite(value) && value>0; })) return false;
        auto scaling=DirectX::XMMatrixScaling(factors[0],factors[1],factors[2]);
        if (factors[0]!=factors[1] || factors[1]!=factors[2])
        {
            const auto frame=DirectX::XMLoadFloat4x4(&axes);
            scaling=DirectX::XMMatrixTranspose(frame)*scaling*frame;
        }
        DirectX::XMFLOAT4X4 matrix;
        DirectX::XMStoreFloat4x4(&matrix,DirectX::XMMatrixTranslation(-pivot[0],-pivot[1],-pivot[2])*
            scaling*DirectX::XMMatrixTranslation(pivot[0],pivot[1],pivot[2]));
        return TransformObjectsWorld(ids,matrix);
    }

    bool SceneWorld::TransformObjectsWorld(const std::vector<std::string>& ids, const DirectX::XMFLOAT4X4& delta)
    {
        if (ids.empty() || !SceneTransforms::IsUsable(delta)) return false;
        auto candidate=layout_;
        std::vector<std::string> seen;
        for (const auto& id : ids)
        {
            const auto found=std::find_if(candidate.objects.begin(),candidate.objects.end(),
                [&](const auto& placement) { return placement.id==id; });
            if (found==candidate.objects.end() || std::find(seen.begin(),seen.end(),id)!=seen.end()) return false;
            seen.push_back(id);
            if (IsIdentity(delta) || HasSelectedAncestor(layout_,found->parentId,ids)) continue;
            DirectX::XMFLOAT4X4 matrix;
            if (!WorldMatrix(id,matrix)) return false;
            DirectX::XMStoreFloat4x4(&matrix,DirectX::XMLoadFloat4x4(&matrix)*DirectX::XMLoadFloat4x4(&delta));
            ScenePlacement placement;
            if (!LocalTransformFromWorld(id,matrix,placement)) return false;
            *found=std::move(placement);
        }
        return CommitTransforms(std::move(candidate));
    }

    std::string SceneWorld::NewId(size_t& nextCounter) const
    {
        for (;;)
        {
            const auto id = "object-" + std::to_string(nextCounter++);
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
            auto nextCounter=nextObjectId_;
            if (placement.id.empty()) placement.id=NewId(nextCounter);
            if (std::any_of(layout_.objects.begin(), layout_.objects.end(),
                [&](const auto& existing) { return existing.id == placement.id; }))
                throw std::runtime_error("Object ID already exists");
            if (placement.name.empty()) placement.name = placement.meshRenderer ? placement.Model().stem().string() : "Empty object";
            auto validation=layout_;
            validation.objects.push_back(placement);
            static_cast<void>(validation.Serialize());
            std::vector<DirectX::XMFLOAT4X4> matrices;
            if (!SceneTransforms::Resolve(validation,matrices,error)) throw std::runtime_error(error);
            Engine::Object3D object;
            if (!object.SetTransform(placement.position, placement.rotation, placement.scale))
                throw std::runtime_error("Invalid transform");
            if (!object.SetWorldMatrix(matrices.back())) throw std::runtime_error("Invalid world matrix");
            if (placement.meshRenderer)
            {
                const auto model = models_.Load(assetsRoot / placement.Model());
                if (!model) throw std::runtime_error("Model could not be loaded");
                object.SetModel(model);
            }
            const auto id = placement.id;
            Append(std::move(placement), std::move(object));
            nextObjectId_=nextCounter;
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

    bool SceneWorld::DuplicateObject(std::string_view id, const std::array<float,3>& offset,
        std::string& createdId, std::string& error)
    {
        std::vector<std::string> created;
        const bool success=DuplicateObjects({std::string(id)},offset,created,error);
        createdId=success ? created.front() : std::string{};
        return success;
    }

    bool SceneWorld::DuplicateObjects(const std::vector<std::string>& ids, const std::array<float,3>& offset,
        std::vector<std::string>& createdIds, std::string& error)
    {
        const auto requested=ids;
        createdIds.clear();
        if (requested.empty()) { error="No objects selected"; return false; }
        std::vector<size_t> indices;
        for (const auto& id : requested)
        {
            const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
                [&](const auto& placement) { return placement.id==id; });
            if (found==layout_.objects.end()) { error=id+": object no longer exists"; return false; }
            const auto index=static_cast<size_t>(found-layout_.objects.begin());
            if (std::find(indices.begin(),indices.end(),index)==indices.end()) indices.push_back(index);
        }
        auto candidate=layout_;
        auto objects=objects_;
        auto nextCounter=nextObjectId_;
        std::unordered_map<std::string,std::string> copies;
        std::vector<std::string> created;
        for (const auto index : indices)
        {
            auto id=NewId(nextCounter);
            copies.emplace(layout_.objects[index].id,id);
            created.push_back(std::move(id));
        }
        for (const auto index : indices)
        {
            auto placement=layout_.objects[index];
            const auto parent=copies.find(placement.parentId);
            if (parent!=copies.end()) placement.parentId=parent->second;
            else if (!TranslatePlacement(placement,offset))
            { error=placement.id+": invalid world duplicate offset"; return false; }
            if(placement.button) {
                const auto remap=[&](std::string& id) {const auto found=copies.find(id); if(found!=copies.end()) id=found->second;};
                if(placement.button->action!="loadScene" && placement.button->action!="setState") remap(placement.button->target);
                remap(placement.button->sound);
            }
            if (placement.prefab)
            {
                const auto root=copies.find(placement.prefab->rootId);
                if (root!=copies.end()) placement.prefab->rootId=root->second;
                else placement.prefab.reset();
            }
            placement.id=copies.at(placement.id); placement.name+=" copy";
            candidate.objects.push_back(std::move(placement));
            objects.push_back(objects_[index]);
        }
        if (!PrepareTransforms(candidate,objects,error)) return false;
        layout_=std::move(candidate); objects_=std::move(objects);
        nextObjectId_=nextCounter;
        createdIds=std::move(created);
        error.clear();
        return true;
    }

    bool SceneWorld::RemoveObjects(const std::vector<std::string>& ids, std::string& error)
    {
        if (ids.empty()) { error="No objects selected"; return false; }
        const std::unordered_set<std::string> removed(ids.begin(),ids.end());
        const auto missing=std::find_if(removed.begin(),removed.end(),[&](const auto& id) {
            return std::none_of(layout_.objects.begin(),layout_.objects.end(),[&](const auto& object) { return object.id==id; });
        });
        if (missing!=removed.end()) { error=*missing+": object no longer exists"; return false; }
        SceneLayout candidate;
        candidate.settings=layout_.settings;
        if (removed.contains(candidate.settings.mainCamera)) candidate.settings.mainCamera.clear();
        std::vector<Engine::Object3D> objects;
        candidate.objects.reserve(layout_.objects.size()); objects.reserve(objects_.size());
        for (size_t index=0;index<layout_.objects.size();++index)
        {
            auto placement=layout_.objects[index];
            if (removed.contains(placement.id)) continue;
            if (placement.prefab && removed.contains(placement.prefab->rootId)) placement.prefab.reset();
            if (removed.contains(placement.parentId) && !ReparentPlacement(placement,{},error)) return false;
            candidate.objects.push_back(std::move(placement));
            objects.push_back(objects_[index]);
        }
        if (!PrepareTransforms(candidate,objects,error)) return false;
        layout_=std::move(candidate); objects_=std::move(objects);
        error.clear();
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
            if (!object.GetModel() || !layout_.objects[index].meshRenderer->enabled) continue;
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

    bool SceneWorld::WorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        auto rotation=DirectX::XMMatrixIdentity();
        auto current=std::string(id);
        size_t remaining=layout_.objects.size();
        do
        {
            const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
                [&](const auto& placement) { return placement.id==current; });
            if (found==layout_.objects.end() || remaining--==0) return false;
            const auto& angles=found->rotation;
            rotation=rotation*DirectX::XMMatrixRotationX(angles[0])*DirectX::XMMatrixRotationY(angles[1])*
                DirectX::XMMatrixRotationZ(angles[2]);
            current=found->parentId;
        } while (!current.empty());
        DirectX::XMFLOAT4X4 candidate;
        DirectX::XMStoreFloat4x4(&candidate,rotation);
        if (!SceneTransforms::IsUsable(candidate)) return false;
        matrix=candidate;
        return true;
    }

    bool SceneWorld::WorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const
    {
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
            [&](const auto& placement) { return placement.id==id; });
        if (found==layout_.objects.end()) return false;
        matrix=objects_[static_cast<size_t>(found-layout_.objects.begin())].GetWorldMatrix();
        return true;
    }

    bool SceneWorld::LocalTransformFromWorld(std::string_view id, const DirectX::XMFLOAT4X4& world, ScenePlacement& placement) const
    {
        const auto found=std::find_if(layout_.objects.begin(),layout_.objects.end(),
            [&](const auto& value) { return value.id==id; });
        if (found==layout_.objects.end()) return false;
        auto matrix=world;
        if (!found->parentId.empty())
        {
            DirectX::XMFLOAT4X4 parent;
            if (!WorldMatrix(found->parentId,parent) || !SceneTransforms::WorldToLocal(world,parent,matrix)) return false;
        }
        return SceneTransforms::ReadTransform(matrix,*found,placement);
    }

    bool SceneWorld::WorldBounds(std::string_view id, std::array<std::array<float, 3>, 8>& corners) const
    {
        const auto found = std::find_if(layout_.objects.begin(), layout_.objects.end(),
            [id](const auto& placement) { return placement.id == id; });
        if (found == layout_.objects.end()) return false;
        const auto& object = objects_[static_cast<size_t>(found - layout_.objects.begin())];
        DirectX::XMFLOAT3 localCorners[8];
        if (object.GetModel()) object.GetModel()->Bounds().GetCorners(localCorners);
        else DirectX::BoundingBox(DirectX::XMFLOAT3{},DirectX::XMFLOAT3{0.25f,0.25f,0.25f}).GetCorners(localCorners);
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
