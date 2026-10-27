#include <SceneRuntime/Rendering/SceneRenderState.h>
#include <SceneRuntime/Rendering/RenderPresentation.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <algorithm>
#include <iterator>

namespace SceneRuntime
{
    SceneGpuResources& SceneRenderState::Gpu() noexcept
    {
        return gpu_;
    }
    const SceneRenderData& SceneRenderState::Read() const noexcept
    {
        return data_;
    }
    SceneResourceView SceneRenderState::View(const SceneLayout& layout) const noexcept
    {
        return {layout,         data_.objects, data_.assetsRoot, data_.assets, data_.materials,
                data_.animated, data_.states,  data_.lods,       data_.shadow, data_.localShadow};
    }
    PreparedSceneResources SceneRenderState::Capture(SceneLayout layout) const
    {
        PreparedSceneResources result;
        static_cast<SceneRenderData&>(result) = data_;
        result.layout = std::move(layout);
        return result;
    }
    bool SceneRenderState::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& shader,
                                      std::string* error)
    {
        if (!gpu_.Initialize(renderer, shader, error))
            return false;
        std::string diagnostic;
        if (!gpu_.PrepareShadows(SceneSettings{}, data_.shadow, data_.localShadow, diagnostic))
        {
            if (error)
                *error = diagnostic;
            return false;
        }
        return true;
    }
    bool SceneRenderState::PrepareShadows(PreparedSceneResources& prepared, std::string& error) const
    {
        if (!prepared.shadow)
            prepared.shadow = data_.shadow;
        if (!prepared.localShadow)
            prepared.localShadow = data_.localShadow;
        return gpu_.PrepareShadows(prepared.layout.settings, prepared.shadow, prepared.localShadow, error);
    }
    bool SceneRenderState::PrepareRetirement(PreparedSceneResources& prepared, std::string& error) const
    {
        if (!prepared.shadow)
            prepared.shadow = data_.shadow;
        if (!prepared.localShadow)
            prepared.localShadow = data_.localShadow;
        const auto& retirement = gpu_.Retirement();
        if (!retirement)
            return true;
        if ((prepared.shadow != data_.shadow && !retirement->Retire(data_.shadow)) ||
            (prepared.localShadow != data_.localShadow && !retirement->Retire(data_.localShadow)))
        {
            error = "Scene shadow retirement failed";
            return false;
        }
        retirement->Collect();
        return true;
    }
    bool SceneRenderState::RetireObjects(const std::vector<Engine::Object3D>& objects) const
    {
        return gpu_.Retirement() && gpu_.Retirement()->Retire(std::make_shared<std::vector<Engine::Object3D>>(objects));
    }
    std::shared_ptr<const RenderPacket> SceneRenderState::PrepareEditor(const PreparedSceneResources& prepared,
                                                                        std::string& error) const
    {
        if (!gpu_.Ready())
            return {};
        return editor_.Prepare(prepared, gpu_.Device().Get(), version_ + 1, error);
    }
    void SceneRenderState::SwapData(SceneRenderData& data) noexcept
    {
        data_.assetsRoot.swap(data.assetsRoot);
        data_.assets.swap(data.assets);
        data_.materials.swap(data.materials);
        data_.objects.swap(data.objects);
        data_.animated.swap(data.animated);
        data_.states.swap(data.states);
        data_.lods.swap(data.lods);
        data_.shadow.swap(data.shadow);
        data_.localShadow.swap(data.localShadow);
        data_.diagnostics.swap(data.diagnostics);
    }
    std::shared_ptr<const RenderPacket> SceneRenderState::Publish(std::shared_ptr<const RenderPacket> packet) noexcept
    {
        packet_.swap(packet);
        editor_.Commit(packet_);
        ++version_;
        return packet;
    }
    std::shared_ptr<const RenderPacket> SceneRenderState::CommitPrepared(PreparedSceneResources& prepared,
                                                                         std::shared_ptr<const RenderPacket> packet,
                                                                         bool preservePreview) noexcept
    {
        SwapData(prepared);
        if (!preservePreview)
            ResetPreview();
        return Publish(std::move(packet));
    }
    std::shared_ptr<const RenderPacket> SceneRenderState::CommitTransforms(
        std::vector<Engine::Object3D>& objects, std::shared_ptr<const RenderPacket> packet) noexcept
    {
        data_.objects.swap(objects);
        return Publish(std::move(packet));
    }
    std::shared_ptr<const RenderPacket> SceneRenderState::CommitFrame(PreparedSceneResources* prepared,
                                                                      std::span<SceneRenderTransform> transforms,
                                                                      std::span<SceneAnimatorFrame> poses,
                                                                      std::shared_ptr<const RenderPacket> packet,
                                                                      SceneRenderState* reload) noexcept
    {
        if (prepared)
            SwapData(*prepared);
        else
            for (auto& transform : transforms)
                data_.objects[transform.index] = std::move(transform.object);
        if (reload)
            gpu_.SwapModels(reload->gpu_);
        for (auto& pose : poses)
            if (!pose.model->ApplyPreparedPose(std::move(pose.pose)))
                std::terminate();
        return Publish(std::move(packet));
    }
    std::shared_ptr<const RenderPacket> SceneRenderState::CommitReload(
        SceneRenderState& candidate, std::shared_ptr<const RenderPacket> packet) noexcept
    {
        gpu_.SwapModels(candidate.gpu_);
        std::swap(gpu_.Occlusion(), candidate.gpu_.Occlusion());
        SwapData(candidate.data_);
        ResetPreview();
        return Publish(std::move(packet));
    }
    const std::shared_ptr<const RenderPacket>& SceneRenderState::Snapshot() const noexcept
    {
        return packet_;
    }
    size_t SceneRenderState::Version() const noexcept
    {
        return version_;
    }
    SceneLayout& SceneRenderState::AnimationScratch(const SceneLayout& layout)
    {
        if (!animationScratch_)
            animationScratch_ = std::make_unique<SceneLayout>();
        *animationScratch_ = layout;
        return *animationScratch_;
    }
    void SceneRenderState::ResetPreview() noexcept
    {
        animationScratch_.reset();
    }
    void SceneRenderState::ApplySurface(size_t index, const ScenePlacement& placement)
    {
        SceneGpuResources::ApplySurfaceOverride(data_.objects[index], placement);
    }
    void SceneRenderState::CommitAnimatorParameters(const std::string& id,
                                                    std::map<std::string, float>& values) noexcept
    {
        data_.states.at(id).parameterOverrides.swap(values);
    }
    void SceneRenderState::EraseAnimatorParameter(const std::string& id, const std::string& name) noexcept
    {
        data_.states.at(id).parameterOverrides.erase(name);
    }
    void SceneRenderState::ReserveAdditionalObject()
    {
        data_.objects.reserve(data_.objects.size() + 1);
    }
    void SceneRenderState::AppendObject(Engine::Object3D object)
    {
        data_.objects.push_back(std::move(object));
        ++version_;
    }
    void SceneRenderState::MergeAnimation(PreparedSceneResources& prepared)
    {
        data_.animated.merge(prepared.animated);
        data_.states.merge(prepared.states);
        data_.lods.merge(prepared.lods);
    }
    void SceneRenderState::CommitDuplicates(
        std::vector<Engine::Object3D>& objects, std::map<std::string, std::shared_ptr<Engine::ModelRenderer>>& animated,
        std::map<std::string, AnimatorState>& states,
        std::map<std::string, std::vector<std::shared_ptr<const Engine::ModelRenderer>>>& lods) noexcept
    {
        data_.objects.swap(objects);
        data_.animated.swap(animated);
        data_.states.swap(states);
        data_.lods.swap(lods);
        ++version_;
    }
    bool SceneRenderState::MatricesCurrent() const noexcept
    {
        return matricesVersion_ == version_;
    }
    bool SceneRenderState::PrepareMatrices(const SceneLayout& layout,
                                           const std::map<std::string, DirectX::XMFLOAT4X4>& overrides,
                                           std::string& error) const
    {
        if (overrides.empty())
        {
            matrices_.clear();
            matrices_.reserve(data_.objects.size());
            std::transform(data_.objects.begin(), data_.objects.end(), std::back_inserter(matrices_),
                           [](const auto& object)
                           {
                               return object.GetWorldMatrix();
                           });
        }
        else if (!SceneTransforms::Resolve(layout, overrides, matrices_, error))
            return false;
        matricesVersion_ = version_;
        return true;
    }
    const std::vector<DirectX::XMFLOAT4X4>& SceneRenderState::Matrices() const noexcept
    {
        return matrices_;
    }
    const MeshTelemetry& SceneRenderState::Telemetry() const noexcept
    {
        return telemetry_;
    }
    bool SceneRenderState::DrawScene(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
                                     const Engine::DirectionalLight& light, const UiState& state) const
    {
        telemetry_ = {};
        return !gpu_.Ready() ||
               RenderScene::Draw(packet_, commands, camera, light, state, gpu_.Occlusion(), telemetry_);
    }
    bool SceneRenderState::DrawPresentation(const PresentationPacket& packet, ID3D12GraphicsCommandList* commands,
                                            unsigned int width, unsigned int height,
                                            const SceneDrawOptions& options) const
    {
        return !gpu_.Ready() ||
               RenderPresentation::Draw(packet, commands, width, height, options, gpu_.Occlusion(), telemetry_);
    }
}
