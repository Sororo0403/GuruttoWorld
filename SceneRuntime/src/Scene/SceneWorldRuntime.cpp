#include "SceneWorldRuntime.h"

#include <Engine/Core/Log.h>
#include <Engine/Core/Profiler.h>
#include "../Rendering/SceneRenderResources.h"
#include <cstring>

namespace SceneRuntime
{
    SceneWorld::RuntimeData::RuntimeData(std::shared_ptr<const WorldDefinition> definition,
                                         std::shared_ptr<const RuntimeScriptDefinitions> scripts)
        : definitions(std::move(scripts)), session(std::move(definition), definitions)
    {
    }
    class SceneWorld::RuntimeProjection final : public RuntimeSystem
    {
        friend class SceneWorld;
        SceneWorld& world_;
        RuntimeData& data_;
        struct Resources final : RenderResourceResolver
        {
            SceneWorld& world;
            WorldState::ReadView view;
            LayoutView prepared;
            const std::vector<AnimatorFrame>& poses;
            std::shared_ptr<Engine::ShadowMap> shadow, localShadow;
            std::shared_ptr<const GpuRenderScene> scene;
            std::vector<size_t> indices;
            std::vector<const Engine::ModelRenderer::PreparedPose*> skins;
            std::vector<const RenderObject*> previous;
            bool reuseBase = false;
            Resources(SceneWorld& target, WorldState::ReadView values, const PreparedLayout& resources,
                      const std::vector<AnimatorFrame>& frames)
                : world(target), view(values),
                  prepared{resources.layout,    resources.objects,  resources.assetsRoot, resources.assets,
                           resources.materials, resources.animated, resources.states,     resources.lods},
                  poses(frames), shadow(resources.shadow), localShadow(resources.localShadow)
            {
                Initialize();
            }
            Resources(SceneWorld& target, WorldState::ReadView values, const std::vector<AnimatorFrame>& frames)
                : world(target), view(values), prepared(target.BaselineView(nullptr)), poses(frames),
                  shadow(target.shadow_), localShadow(target.localShadow_), reuseBase(true)
            {
                Initialize();
            }
            void Initialize()
            {
                const auto entities = view.Entities();
                size_t slots = 0;
                for (const auto entity : entities)
                    slots = std::max(slots, static_cast<size_t>(entity.slot) + 1);
                indices.assign(slots, static_cast<size_t>(-1));
                skins.resize(slots);
                previous.resize(slots);
                if (entities.size() != prepared.layout.objects.size())
                    throw std::runtime_error("Render definition and runtime counts differ");
                for (size_t index = 0; index < entities.size(); ++index)
                    indices[entities[index].slot] = index;
                for (const auto& pose : poses)
                    skins[view.Resolve(pose.id)->slot] = &pose.pose;
                const auto previousPacket = world.RenderSnapshot();
                if (previousPacket)
                {
                    for (const auto& object : previousPacket->objects)
                        if (object.entity.slot < slots)
                            previous[object.entity.slot] = &object;
                    const auto oldScene =
                        std::dynamic_pointer_cast<const GpuRenderScene>(previousPacket->sceneResources);
                    if (oldScene && oldScene->shadow == shadow && oldScene->localShadow == localShadow)
                        scene = oldScene;
                }
                if (!scene)
                {
                    auto candidate = std::make_shared<GpuRenderScene>();
                    candidate->shadow = shadow;
                    candidate->localShadow = localShadow;
                    scene = std::move(candidate);
                }
            }
            std::shared_ptr<const RenderResource> Scene() const override
            {
                return scene;
            }
            std::optional<RenderBinding> Resolve(EntityHandle entity, std::string& error) override
            {
                if (entity.slot >= indices.size() || indices[entity.slot] == static_cast<size_t>(-1))
                {
                    error = "Render object definition missing";
                    return {};
                }
                const auto index = indices[entity.slot];
                const auto& id = prepared.layout.objects[index].id;
                const auto* old = previous[entity.slot];
                const auto* previousResource =
                    old ? dynamic_cast<const GpuRenderObject*>(old->binding.resource.get()) : nullptr;
                const bool sameOwner = old && old->entity == entity && previousResource &&
                                       previousResource->device.Get() == world.materialDevice_.Get();
                const auto& base = reuseBase && sameOwner ? previousResource->object : prepared.objects.at(index);
                if (!base.GetModel())
                {
                    error = "Render model missing";
                    return {};
                }
                std::optional<Engine::Object3D> modified;
                const auto surface = view.ComponentLease<MaterialComponent>(entity);
                if (surface && surface->enabled && surface->surfaceOverride)
                {
                    const auto matches = [&](const auto& material)
                    {
                        return material && material->color == surface->color &&
                               material->roughness == surface->roughness && material->metallic == surface->metallic &&
                               material->shaderParameters == surface->shaderParameters;
                    };
                    const bool changed = !matches(base.GetMaterial()) ||
                                         std::any_of(base.GetMaterialSlots().begin(), base.GetMaterialSlots().end(),
                                                     [&](const auto& slot)
                                                     {
                                                         return slot && !matches(slot);
                                                     });
                    if (changed)
                    {
                        modified.emplace(base);
                        const auto apply = [&](const std::shared_ptr<const Engine::Material>& original)
                            -> std::shared_ptr<const Engine::Material>
                        {
                            if (matches(original))
                                return original;
                            auto material = original ? std::make_shared<Engine::Material>(*original)
                                                     : std::make_shared<Engine::Material>();
                            material->color = surface->color;
                            material->roughness = surface->roughness;
                            material->metallic = surface->metallic;
                            material->shaderParameters = surface->shaderParameters;
                            return material;
                        };
                        modified->SetMaterial(apply(base.GetMaterial()));
                        auto slots = base.GetMaterialSlots();
                        for (auto& slot : slots)
                            if (slot)
                                slot = apply(slot);
                        modified->SetMaterialSlots(std::move(slots));
                    }
                }
                const auto& object = modified ? *modified : base;
                const auto levels = prepared.lods.find(id);
                RenderBinding binding;
                if (old && old->entity == entity && previousResource &&
                    previousResource->device.Get() == world.materialDevice_.Get() &&
                    previousResource->object.GetModel() == object.GetModel() &&
                    previousResource->object.GetMaterial() == object.GetMaterial() &&
                    previousResource->object.GetMaterialSlots() == object.GetMaterialSlots() &&
                    (levels == prepared.lods.end() ? previousResource->levels.empty()
                                                   : previousResource->levels == levels->second))
                {
                    binding = old->binding;
                    binding.skin.reset();
                }
                else
                {
                    auto resource = std::make_shared<GpuRenderObject>();
                    resource->object = object;
                    resource->device = world.materialDevice_;
                    if (levels != prepared.lods.end())
                        resource->levels = levels->second;
                    binding.device = reinterpret_cast<std::uintptr_t>(world.materialDevice_.Get());
                    binding.generation = world.renderVersion_ + 1;
                    const auto bounds = object.GetModel()->Bounds();
                    binding.bounds = {{bounds.Center.x, bounds.Center.y, bounds.Center.z},
                                      {bounds.Extents.x, bounds.Extents.y, bounds.Extents.z}};
                    binding.resource = std::move(resource);
                }
                if (const auto* pose = skins[entity.slot])
                {
                    auto skin = std::make_shared<RenderSkin>();
                    skin->palettes = pose->Palettes();
                    const auto bounds = pose->Bounds();
                    skin->bounds = {{bounds.Center.x, bounds.Center.y, bounds.Center.z},
                                    {bounds.Extents.x, bounds.Extents.y, bounds.Extents.z}};
                    binding.skin = std::move(skin);
                }
                return binding;
            }
        };
        struct Candidate final : RuntimeSystem::Frame
        {
            SceneWorld& world;
            RuntimeData& data;
            WorldState::Transaction& transaction;
            PreparedLayout prepared;
            std::vector<AnimatorFrame> poses;
            RuntimeScriptEffects effects;
            SceneChangeSet publication;
            bool valuesOnly = false, sparse = false;
            struct ValuePublication
            {
                size_t index;
                ScenePlacement value;
            };
            struct TransformPublication
            {
                size_t index;
                LocalTransform local;
                Engine::Object3D object;
            };
            std::vector<ValuePublication> values;
            std::vector<TransformPublication> transforms;
            double interpolation = 0;
            std::shared_ptr<const RenderPacket> packet;
            std::map<std::string, PhysicalPose> previousPhysical, currentPhysical;
            Candidate(SceneWorld& target, RuntimeData& runtime, WorldState::Transaction& tx)
                : world(target), data(runtime), transaction(tx)
            {
            }
            bool Fixed(double, const RuntimeInput&, const RuntimeScriptEffects&, std::string&) override
            {
                return true;
            }
            bool Update(double, const RuntimeScriptEffects&, std::string&) override
            {
                return true;
            }
            void SetInterpolation(double value) override
            {
                interpolation = value;
            }
            bool CanUseValues(const SceneChangeSet& changes) const
            {
                if (data.reset || data.reloading || !world.assetIndex_ || world.assetsRoot_ != data.root ||
                    transaction.Read().Definition() != data.session.Read().Definition() || changes.structureChanged ||
                    !changes.resources.empty() || world.prepareRuntime_ || world.prepareUi_)
                    return false;
                for (const auto& field : changes.fields)
                    if (field.objectId.empty() && !field.field.starts_with("runtime."))
                        return false;
                for (const auto& component : changes.components)
                    if (component.componentType == "meshRenderer" || component.componentType == "terrain" ||
                        component.componentType == "tilemap" || component.componentType == "animator" ||
                        component.componentType == "canvas")
                        return false;
                for (const auto& component : changes.components)
                    if (component.componentType == "material")
                    {
                        const auto entity = transaction.Read().Resolve(component.objectId);
                        if (!entity)
                            return false;
                        const auto before = data.session.Read().ComponentLease<MaterialComponent>(*entity);
                        const auto after = transaction.Read().ComponentLease<MaterialComponent>(*entity);
                        if (!before || !after || before->enabled != after->enabled || before->asset != after->asset ||
                            before->slots != after->slots || (before->surfaceOverride && !after->surfaceOverride))
                            return false;
                    }
                return true;
            }
            bool PrepareValues(const SceneChangeSet& changes, std::string& error)
            {
                Engine::CpuScope scope("Sparse display value preparation");
                const auto view = transaction.Read();
                const auto entities = view.Entities();
                if (entities.size() != world.layout_.objects.size() || entities.size() != world.objects_.size())
                {
                    error = "Sparse projection counts differ";
                    return false;
                }
                size_t slots = 0;
                for (const auto entity : entities)
                    slots = std::max(slots, static_cast<size_t>(entity.slot) + 1);
                std::vector<size_t> indices(slots, static_cast<size_t>(-1));
                for (size_t index = 0; index < entities.size(); ++index)
                    indices[entities[index].slot] = index;
                std::vector<unsigned char> affected(entities.size());
                const auto mark = [&](const std::string& id)
                {
                    const auto entity = view.Resolve(id);
                    if (!entity)
                        throw std::runtime_error("Sparse projection owner missing");
                    affected[indices[entity->slot]] = 1;
                };
                for (const auto& component : changes.components)
                    mark(component.objectId);
                for (const auto& object : changes.objects)
                    mark(object.objectId);
                for (const auto& field : changes.fields)
                    if (!field.objectId.empty() && !field.field.starts_with("runtime."))
                        mark(field.objectId);
                values.reserve(
                    static_cast<size_t>(std::count(affected.begin(), affected.end(), static_cast<unsigned char>(1))));
                transforms.reserve(changes.transforms.size());
                for (size_t index = 0; index < entities.size(); ++index)
                {
                    if (affected[index])
                    {
                        auto value = *view.Object(entities[index]);
                        const auto& previous = world.layout_.objects[index];
                        bool sameShape = previous.prefab.has_value() == value.prefab.has_value();
#define ENGINE_SPARSE_OPTIONAL(Type, member, references, assets, order)                                                \
    sameShape &= previous.member.has_value() == value.member.has_value();
#define ENGINE_SPARSE_REPEATED(Type, member, references, assets, order)
                        ENGINE_SCENE_COMPONENTS(ENGINE_SPARSE_OPTIONAL, ENGINE_SPARSE_REPEATED)
#undef ENGINE_SPARSE_OPTIONAL
#undef ENGINE_SPARSE_REPEATED
                        if (!sameShape)
                        {
                            error = "Sparse projection component shape changed";
                            return false;
                        }
                        values.push_back({index, std::move(value)});
                    }
                    const auto local = *view.Transform(entities[index]);
                    const auto matrix = view.WorldMatrix(entities[index]);
                    if (!matrix)
                    {
                        error = "Sparse projection world matrix missing";
                        return false;
                    }
                    const auto& placement = world.layout_.objects[index];
                    const bool changedLocal = placement.position != local.position ||
                                              placement.rotation != local.rotation || placement.scale != local.scale;
                    if (!changedLocal &&
                        std::memcmp(&*matrix, &world.objects_[index].GetWorldMatrix(), sizeof(*matrix)) == 0)
                        continue;
                    auto object = world.objects_[index];
                    if ((changedLocal && !object.SetTransform(local.position, local.rotation, local.scale)) ||
                        !object.SetWorldMatrix(*matrix))
                    {
                        error = "Sparse projection transform invalid";
                        return false;
                    }
                    transforms.push_back({index, local, std::move(object)});
                }
                return true;
            }
            bool Prepare(const SceneChangeSet& changes, const RuntimeScriptEffects& commands,
                         std::string& error) override
            {
                Engine::CpuScope scope("Runtime display projection");
                effects = data.effects;
                effects.scenes.insert(effects.scenes.end(), commands.scenes.begin(), commands.scenes.end());
                effects.audio.insert(effects.audio.end(), commands.audio.begin(), commands.audio.end());
                for (const auto& [key, value] : commands.ui.values)
                    effects.ui.values[key] = value;
                for (const auto& [key, value] : commands.ui.texts)
                    effects.ui.texts[key] = value;
                publication = world.changes_;
                publication.Merge(changes);
                if (data.reset)
                    publication.fields.push_back({{}, {}, "runtime.reset"});
                if (data.reloading)
                    publication.fields.push_back({{}, {}, "resources.reload"});
                if (!data.skipHostPreparation && world.prepareFrame_ &&
                    !world.prepareFrame_(transaction.Read(), changes, effects.ui, error))
                    return false;
                if (data.pauseRequest && transaction.Changes().Empty())
                {
                    valuesOnly = true;
                    error.clear();
                    return true;
                }

                sparse = CanUseValues(changes);
                if (sparse)
                {
                    if (!PrepareValues(changes, error))
                        return false;
                }
                else
                {
                    auto layout = transaction.Read().ExportLayout();
                    if (data.reloading)
                    {
                        auto& source = *data.reloading;
                        prepared.layout = source.layout_;
                        prepared.assetsRoot = source.assetsRoot_;
                        prepared.assets = source.assetIndex_;
                        prepared.materials = source.materials_;
                        prepared.objects = source.objects_;
                        prepared.animated = source.animatedModels_;
                        prepared.states = source.animatorStates_;
                        prepared.lods = source.lodModels_;
                        prepared.diagnostics = source.resourceDiagnostics_;
                        prepared.shadow = source.shadow_;
                        prepared.localShadow = source.localShadow_;
                    }
                    else if (world.assetIndex_ && world.assetsRoot_ == data.root)
                    {
                        if (!world.PrepareScriptLayout(std::move(layout), nullptr, prepared, error))
                            return false;
                    }
                    else if (!world.PrepareLayout(std::move(layout), data.root, error, true, prepared))
                        return false;
                    if (!world.PrepareShadows(prepared.layout.settings, prepared.shadow, prepared.localShadow, error))
                        return false;
                    if (world.retirement_)
                    {
                        if (prepared.shadow != world.shadow_ && !world.retirement_->Retire(world.shadow_))
                            return false;
                        if (prepared.localShadow != world.localShadow_ &&
                            !world.retirement_->Retire(world.localShadow_))
                            return false;
                    }
                }
                if (!sparse)
                    prepared.states.clear();
                const auto& animated = sparse ? world.animatedModels_ : prepared.animated;
                for (const auto& skin : data.animation->CandidateSnapshot())
                {
                    const auto id = *transaction.Read().ObjectId(skin.entity);
                    const auto model = animated.find(id);
                    if (model == animated.end())
                        throw std::runtime_error("Runtime animator display missing");
                    AnimatorFrame frame;
                    frame.id = id;
                    frame.model = model->second;
                    if (frame.model->Rig() != skin.rig)
                        throw std::runtime_error("CPU and GPU skeleton artifact generations differ");
                    if (!frame.model->PreparePose(skin.state->pose, frame.pose))
                        throw std::runtime_error("Runtime skin pose invalid");
                    frame.applyPose = true;
                    poses.push_back(std::move(frame));
                    if (!sparse)
                        prepared.states.emplace(id, *skin.state);
                }
                auto resources = sparse ? std::make_unique<Resources>(world, transaction.Read(), poses)
                                        : std::make_unique<Resources>(world, transaction.Read(), prepared, poses);
                packet = RenderExtractor::Extract(transaction.Read(), *resources, world.renderVersion_ + 1,
                                                  reinterpret_cast<std::uintptr_t>(world.materialDevice_.Get()),
                                                  data.physics->History(), interpolation, error);
                if (!packet)
                    return false;
                const auto capture = [&](const auto& source, auto& target)
                {
                    const auto view = transaction.Read();
                    for (const auto& pose : source)
                    {
                        if (!view.Alive(pose.entity) || (pose.body.generation && !view.Alive(pose.body)) ||
                            (pose.controller.generation && !view.Alive(pose.controller)))
                            continue;
                        const auto parent = view.ObjectId(pose.parent);
                        target.emplace(*view.ObjectId(pose.entity),
                                       PhysicalPose{pose.transform.position, pose.transform.rotation,
                                                    pose.transform.scale, parent.value_or(std::string{})});
                    }
                };
                capture(data.physics->History().previous, previousPhysical);
                capture(data.physics->History().current, currentPhysical);
                if (!sparse && !data.skipHostPreparation && world.prepareRuntime_ &&
                    !world.prepareRuntime_(prepared.layout, error))
                    return false;
                if (!sparse && !data.skipHostPreparation && world.prepareUi_ &&
                    !world.prepareUi_(prepared.layout, effects.ui, error))
                    return false;
                for (const auto& pose : poses)
                    publication.fields.push_back({pose.id, {}, "runtime.animator"});
                error.clear();
                return true;
            }
            void Commit() noexcept override
            {
                data.effects.Swap(effects);
                world.changes_.Swap(publication);
                if (valuesOnly)
                    return;
                data.packet.swap(packet);
                if (sparse)
                {
                    // 同じ構成の既存領域へ代入し、Debugのmap移動構築を確定経路へ持ち込みません。
                    for (auto& value : values)
                        world.layout_.objects[value.index] = std::move(value.value);
                    for (auto& transform : transforms)
                    {
                        auto& placement = world.layout_.objects[transform.index];
                        placement.position = transform.local.position;
                        placement.rotation = transform.local.rotation;
                        placement.scale = transform.local.scale;
                        world.objects_[transform.index] = std::move(transform.object);
                    }
                }
                else
                {
                    world.layout_.Swap(prepared.layout);
                    world.resourceDiagnostics_.swap(prepared.diagnostics);
                    world.assetsRoot_.swap(prepared.assetsRoot);
                    world.assetIndex_.swap(prepared.assets);
                    world.materials_.swap(prepared.materials);
                    world.objects_.swap(prepared.objects);
                    world.animatedModels_.swap(prepared.animated);
                    world.animatorStates_.swap(prepared.states);
                    world.lodModels_.swap(prepared.lods);
                    world.shadow_.swap(prepared.shadow);
                    world.localShadow_.swap(prepared.localShadow);
                }
                if (data.reloading)
                {
                    world.models_.Swap(data.reloading->models_);
                    world.modelsReady_ = data.reloading->modelsReady_;
                    world.meshShader_.swap(data.reloading->meshShader_);
                }
                data.previousPhysical.swap(previousPhysical);
                data.currentPhysical.swap(currentPhysical);
                for (auto& pose : poses)
                    if (!pose.model->ApplyPreparedPose(std::move(pose.pose)))
                        std::terminate();
                ++world.renderVersion_;
            }
            void Finish() noexcept override {}
        };

    public:
        /// <summary>CPUの実行値を、公開前に表示資源へ投影します。</summary>
        RuntimeProjection(SceneWorld& world, RuntimeData& data) : world_(world), data_(data) {}
        /// <summary>表示候補を作成します。</summary>
        std::unique_ptr<Frame> Begin(WorldState::Transaction& transaction) override
        {
            return std::make_unique<Candidate>(world_, data_, transaction);
        }
        /// <summary>表示システムは物理問い合わせを提供しません。</summary>
        RuntimeScriptQueries Queries() const override
        {
            return {};
        }
    };
    std::shared_ptr<const RenderPacket> SceneWorld::PrepareDocumentPacket(const PreparedLayout& prepared,
                                                                          std::string& error)
    {
        if (!modelsReady_)
            return {};
        WorldState values(WorldDefinition::Compile(prepared.layout));
        std::vector<AnimatorFrame> poses;
        for (const auto& [id, model] : prepared.animated)
        {
            if (!values.Read().Resolve(id))
                continue;
            AnimatorFrame frame;
            frame.id = id;
            frame.model = model;
            if (!model->PreparePose(prepared.states.at(id).pose, frame.pose))
            {
                error = "Document display pose invalid";
                return {};
            }
            poses.push_back(std::move(frame));
        }
        RuntimeProjection::Resources resources(*this, values.Read(), prepared, poses);
        return RenderExtractor::Extract(values.Read(), resources, renderVersion_ + 1,
                                        reinterpret_cast<std::uintptr_t>(materialDevice_.Get()), {}, 0, error);
    }
    std::shared_ptr<const RenderPacket> SceneWorld::PrepareDocumentPacket(const SceneLayout& layout,
                                                                          const std::vector<Engine::Object3D>& objects,
                                                                          std::string& error)
    {
        PreparedLayout prepared;
        prepared.layout = layout;
        prepared.objects = objects;
        prepared.animated = animatedModels_;
        prepared.states = animatorStates_;
        prepared.lods = lodModels_;
        prepared.shadow = shadow_;
        prepared.localShadow = localShadow_;
        return PrepareDocumentPacket(prepared, error);
    }
    std::shared_ptr<const RenderPacket> SceneWorld::PrepareRuntimePacket(const PreparedLayout& prepared,
                                                                         std::string& error)
    {
        const auto view = runtime_->session.Read();
        std::vector<AnimatorFrame> poses;
        for (const auto& skin : runtime_->animation->Snapshot())
        {
            AnimatorFrame frame;
            frame.id = *view.ObjectId(skin.entity);
            frame.model = prepared.animated.at(frame.id);
            if (!frame.model->PreparePose(skin.state->pose, frame.pose))
            {
                error = "Reloaded runtime pose invalid";
                return {};
            }
            poses.push_back(std::move(frame));
        }
        RuntimeProjection::Resources resources(*this, view, prepared, poses);
        return RenderExtractor::Extract(view, resources, renderVersion_ + 1,
                                        reinterpret_cast<std::uintptr_t>(materialDevice_.Get()),
                                        runtime_->physics->History(), runtime_->session.Interpolation(), error);
    }
    void SceneWorld::SetFramePreparation(
        std::function<bool(WorldState::ReadView, const SceneChangeSet&, const ScriptUiCommands&, std::string&)> prepare)
    {
        if (!CanMutateScene())
            return;
        prepareFrame_ = std::move(prepare);
    }
    bool SceneWorld::ReplaceSession(SceneLayout layout, const std::filesystem::path& root, bool start,
                                    std::string& error)
    {
        if (!CanMutateScene(&error))
            return false;
        ChangePreparation preparing(preparingChanges_);
        try
        {
            if (context_)
                context_->RequireRoot(root);
            // 同一値の初回Editでも、保存IDの解決を飛ばさず不変定義へ固定します。
            layout.ResolveAssets(
                Engine::AssetDatabase(context_ ? context_ : Engine::ContentContext::Capture(root), true));
            auto definitions = runtime_ ? runtime_->definitions : RuntimeScriptRegistry::Capture();
            auto candidate = std::make_unique<RuntimeData>(WorldDefinition::Compile(layout), std::move(definitions));
            candidate->root = context_ ? context_->Root() : root;
            candidate->reset = true;
            candidate->skipHostPreparation = !start;
            candidate->session.SetContent(context_, candidate->root);
            auto simulation = RuntimeSimulation::Create(candidate->root, context_);
            candidate->physics = simulation.physics;
            candidate->animation = simulation.animation;
            simulation.systems.push_back(std::make_unique<RuntimeProjection>(*this, *candidate));
            simulation.Attach(candidate->session);
            const bool ready = start ? candidate->session.Start(error)
                                     : candidate->session.Edit(ReplaceDocumentEdit{std::move(layout)}, error);
            if (!ready)
                return false;
            candidate->reset = false;
            candidate->skipHostPreparation = false;
            runtime_.swap(candidate);
            animationScratch_.reset();
            runtime_->previousPhysical.clear();
            runtime_->currentPhysical.clear();
            runtime_->fixedSeconds = 0;
            DrainSession();
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
        catch (...)
        {
            error = "Runtime replacement preparation failed";
            return false;
        }
    }
    bool SceneWorld::CheckSessionRoot(const std::filesystem::path& root, std::string& error) const
    {
        try
        {
            context_->RequireRoot(root);
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
    bool SceneWorld::EditSession(const SceneEdit& edit, std::string& error, bool start)
    {
        if (!runtime_ || !CanMutateScene(&error))
            return false;
        ChangePreparation preparing(preparingChanges_);
        const bool success = runtime_->session.Edit(edit, error, start);
        if (success)
            DrainSession();
        return success;
    }
    bool SceneWorld::ApplySessionEffects(const RuntimeScriptEffects& effects)
    {
        if (!CanMutateScene())
            return false;
        ChangePreparation preparing(preparingChanges_);
        std::string error;
        const bool success = runtime_->session.ApplyEffects(effects, error);
        if (success)
            DrainSession();
        else if (!error.empty())
            Engine::Log::Warning(error);
        return success;
    }
    void SceneWorld::DrainSession() noexcept
    {
        runtime_->session.DrainEffects(runtime_->drained);
        runtime_->session.TakeChanges();
        runtime_->fixedSeconds = runtime_->session.Interpolation() * FixedStep();
    }
    bool SceneWorld::UpdateSession(double seconds, float horizontal, float vertical, bool jump, bool movement,
                                   bool components)
    {
        if (!CanMutateScene())
            return false;
        ChangePreparation preparing(preparingChanges_);
        auto& input = runtime_->input;
        input.horizontal = horizontal;
        input.vertical = vertical;
        input.jump = jump;
        std::string error;
        const bool success = components ? runtime_->session.Tick(seconds, input, movement, error)
                                        : runtime_->session.TickFixed(seconds, input, error);
        if (success)
            DrainSession();
        else if (!error.empty())
            Engine::Log::Warning(error);
        return success;
    }
    const AnimatorState* SceneWorld::AnimatorStatus(const std::string& id) const
    {
        if (runtime_)
        {
            const auto view = runtime_->session.Read();
            const auto entity = view.Resolve(id);
            const auto instance = entity ? view.Instance<AnimatorComponent>(*entity) : std::nullopt;
            return instance ? runtime_->animation->Status(*instance) : nullptr;
        }
        const auto found = animatorStates_.find(id);
        return found == animatorStates_.end() ? nullptr : &found->second;
    }
    std::string SceneWorld::AnimatorStateName(const std::string& id) const
    {
        const auto* state = AnimatorStatus(id);
        return state ? state->current : std::string{};
    }
    std::shared_ptr<const RenderPacket> SceneWorld::RenderSnapshot() const noexcept
    {
        return runtime_ ? runtime_->packet : documentPacket_;
    }
    bool SceneWorld::ValidateScripts(std::string& error) const
    {
        if (runtime_)
            return runtime_->session.Validate(error);
        if (!runtimeOwner_)
        {
            error = "Editing document has no runtime session";
            return false;
        }
        WorldState candidate(WorldDefinition::Compile(layout_));
        return RuntimeScripts(RuntimeScriptRegistry::Capture()).Validate(candidate.Read(), error);
    }
    bool SceneWorld::HasScriptPhase(ScriptPhase phase) const
    {
        if (runtime_)
            return runtime_->session.HasPhase(phase);
        if (!runtimeOwner_)
            return false;
        WorldState candidate(WorldDefinition::Compile(layout_));
        return RuntimeScripts(RuntimeScriptRegistry::Capture()).HasPhase(candidate.Read(), phase);
    }
    std::vector<SceneCommand> SceneWorld::TakeSceneCommands()
    {
        if (!CanMutateScene())
            return {};
        if (!runtime_)
            return {};
        std::vector<SceneCommand> result;
        result.swap(runtime_->effects.scenes);
        return result;
    }
    ScriptUiCommands SceneWorld::TakeUiCommands()
    {
        if (!CanMutateScene())
            return {};
        if (!runtime_)
            return {};
        ScriptUiCommands result;
        result.values.swap(runtime_->effects.ui.values);
        result.texts.swap(runtime_->effects.ui.texts);
        return result;
    }
    std::vector<AudioCommand> SceneWorld::TakeAudioCommands()
    {
        if (!CanMutateScene())
            return {};
        if (!runtime_)
            return {};
        std::vector<AudioCommand> result;
        result.swap(runtime_->effects.audio);
        return result;
    }
    std::optional<PhysicsRayHit> SceneWorld::PhysicsRaycast(const std::array<float, 3>& origin,
                                                            const std::array<float, 3>& direction, float distance,
                                                            unsigned int mask, bool triggers,
                                                            const std::string& ignore) const
    {
        if (runtime_)
        {
            const auto queries = runtime_->physics->Queries();
            return queries.raycast ? queries.raycast(origin, direction, distance, mask, triggers, ignore)
                                   : std::nullopt;
        }
        return {};
    }
}
