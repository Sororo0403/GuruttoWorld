#include <SceneRuntime/Animation/RuntimeAnimation.h>
#include <Engine/Animation/SkeletonAssets.h>
#include <Engine/Core/Profiler.h>
#include <SceneRuntime/Physics/RuntimeRagdoll.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace SceneRuntime
{
    struct RuntimeAnimation::Impl
    {
        struct Entry
        {
            ComponentInstanceHandle instance;
            std::shared_ptr<const AnimatorComponent> component;
            std::filesystem::path model;
            std::shared_ptr<const Engine::SkeletonData> rig;
            AnimatorState state;
            std::uint64_t version = 0;
        };
        using Entries = std::map<std::uint64_t, std::shared_ptr<const Entry>>;
        std::filesystem::path root;
        RuntimeAnimationServices services;
        Entries entries;
        struct Candidate;
        Candidate* active = nullptr;
        static std::vector<RuntimeSkinPose> Snapshot(const Entries& entries)
        {
            std::vector<RuntimeSkinPose> result;
            for (const auto& [key, entry] : entries)
            {
                static_cast<void>(key);
                result.push_back({entry->instance.entity, entry->instance, entry->rig,
                                  std::shared_ptr<const AnimatorState>(entry, &entry->state), entry->version});
            }
            return result;
        }
        struct Candidate final : RuntimeSystem::Frame
        {
            Impl& owner;
            WorldState::Transaction& transaction;
            Entries entries;
            std::set<std::uint64_t> writable;
            RuntimeInput input;
            std::vector<ScriptEvent> events;
            bool changed = false;
            bool updating = false;
            Candidate(Impl& value, WorldState::Transaction& tx) : owner(value), transaction(tx), entries(value.entries)
            {
                if (owner.active)
                    throw std::logic_error("Reentrant animation frame");
                owner.active = this;
            }
            ~Candidate()
            {
                if (owner.active == this)
                    owner.active = nullptr;
            }
            Entry& Write(std::uint64_t key)
            {
                auto& entry = entries.at(key);
                if (writable.insert(key).second || entry.use_count() > 1)
                    entry = std::make_shared<Entry>(*entry);
                return const_cast<Entry&>(*entry);
            }
            void Reconcile(bool reload = false)
            {
                const auto view = transaction.Read();
                std::set<std::uint64_t> activeKeys;
                for (const auto entity : view.With<AnimatorComponent>())
                {
                    const auto instance = *view.Instance<AnimatorComponent>(entity);
                    const auto component = view.ComponentLease<AnimatorComponent>(entity);
                    const auto mesh = view.ComponentLease<MeshRendererComponent>(entity);
                    if (!mesh)
                        throw std::runtime_error("Animator needs a MeshRenderer");
                    const auto key = instance.generation;
                    activeKeys.insert(key);
                    const auto previous = entries.find(key);
                    if (previous != entries.end() && previous->second->instance == instance &&
                        previous->second->model == mesh->model && *previous->second->component == *component && !reload)
                        continue;
                    auto entry = std::make_shared<Entry>();
                    entry->instance = instance;
                    entry->component = component;
                    entry->model = mesh->model;
                    std::string error;
                    entry->rig = owner.services.load ? owner.services.load(owner.root / mesh->model, error)
                                                     : Engine::SkeletonAssets::Load(owner.root / mesh->model, error);
                    if (!entry->rig)
                        throw std::runtime_error(error.empty() ? "Animator skeleton missing" : error);
                    if (previous != entries.end() && previous->second->instance == instance &&
                        previous->second->model == mesh->model && previous->second->rig == entry->rig &&
                        *previous->second->component == *component)
                        continue;
                    if (previous != entries.end() && previous->second->instance == instance)
                    {
                        entry->state.parameterOverrides = previous->second->state.parameterOverrides;
                        entry->state.ikOverrides = previous->second->state.ikOverrides;
                        entry->state.rootMotionOverride = previous->second->state.rootMotionOverride;
                    }
                    const bool sameArtifact = previous != entries.end() && previous->second->instance == instance &&
                                              *previous->second->component == *component &&
                                              Engine::SkeletonAssets::SameArtifact(previous->second->rig, entry->rig);
                    std::shared_ptr<const Entry> inherited;
                    if (!sameArtifact && previous == entries.end())
                        if (const auto source = transaction.DuplicateSource(entity))
                            for (const auto& [sourceKey, candidate] : entries)
                            {
                                static_cast<void>(sourceKey);
                                if (candidate->instance.entity == *source && *candidate->component == *component &&
                                    Engine::SkeletonAssets::SameArtifact(candidate->rig, entry->rig))
                                {
                                    inherited = candidate;
                                    break;
                                }
                            }
                    if (sameArtifact)
                        entry->state = previous->second->state;
                    else if (inherited)
                        entry->state = inherited->state;
                    Animator::Validate(*component, entry->rig.get());
                    RuntimeRagdoll::Validate(view, entity, *entry->rig);
                    const auto matrix = *view.WorldMatrix(entity);
                    if (!sameArtifact && !inherited)
                        Animator::Advance(*component, entry->state, *entry->rig, 0, {}, &matrix);
                    entry->version = sameArtifact ? previous->second->version : inherited ? inherited->version : 1;
                    entries.insert_or_assign(key, std::move(entry));
                    writable.insert(key);
                    changed = true;
                }
                std::erase_if(entries,
                              [&](const auto& entry)
                              {
                                  const bool removed = !activeKeys.contains(entry.first);
                                  changed |= removed;
                                  return removed;
                              });
            }
            void Overrides(Entry& entry, const RuntimeScriptEffects& effects)
            {
                const auto id = *transaction.Read().ObjectId(entry.instance.entity);
                if (const auto found = effects.clearAnimatorParameters.find(id);
                    found != effects.clearAnimatorParameters.end())
                    for (const auto& name : found->second)
                        entry.state.parameterOverrides.erase(name);
                if (const auto found = effects.rootMotions.find(id); found != effects.rootMotions.end())
                    entry.state.rootMotionOverride = found->second;
                if (const auto found = effects.animatorParameters.find(id); found != effects.animatorParameters.end())
                    for (const auto& [name, value] : found->second)
                        entry.state.parameterOverrides[name] = value;
                Animator::ValidateParameters(entry.state.parameterOverrides);
                if (const auto found = effects.ikTargets.find(id); found != effects.ikTargets.end())
                    for (const auto& [name, value] : found->second)
                        if (value)
                        {
                            Animator::ValidateIkTarget(*value);
                            entry.state.ikOverrides[name] = *value;
                        }
                        else
                            entry.state.ikOverrides.erase(name);
            }
            std::map<std::string, float> Parameters(EntityHandle entity) const
            {
                auto parameters = input.values;
                const auto& mapping = transaction.Read().Definition()->Source().settings.playerInputs;
                const auto value = [&](const auto& name)
                {
                    const auto found = input.values.find(name);
                    return found == input.values.end() ? 0.f : found->second;
                };
                parameters["moveX"] = value(mapping[0]) - value(mapping[1]);
                parameters["moveY"] = value(mapping[2]) - value(mapping[3]);
                parameters["speed"] = std::hypot(parameters["moveX"], parameters["moveY"]);
                parameters["grounded"] =
                    owner.services.grounded ? owner.services.grounded(transaction.Read(), entity) : 1.f;
                for (const auto& [name, pressed] : input.pressed)
                    parameters["pressed:" + name] = pressed ? 1.f : 0.f;
                return parameters;
            }
            void Evaluate(double seconds, const RuntimeScriptEffects& effects)
            {
                Reconcile();
                std::optional<Engine::CpuScope> timing;
                if (!entries.empty())
                    timing.emplace("Animator evaluation");
                std::vector<RuntimePose> roots;
                for (const auto& [key, value] : entries)
                {
                    static_cast<void>(value);
                    auto& entry = Write(key);
                    const auto entity = entry.instance.entity;
                    const auto view = transaction.Read();
                    Overrides(entry, effects);
                    const auto ragdoll = view.ComponentLease<RagdollComponent>(entity);
                    const bool physical = ragdoll && ragdoll->enabled && ragdoll->active && !ragdoll->bones.empty();
                    const auto matrix = *view.WorldMatrix(entity);
                    if (physical)
                    {
                        entry.state.pose = RuntimeRagdoll::Pose(view, entity, *entry.rig,
                                                                entry.state.basePose.empty() ? entry.state.pose
                                                                                             : entry.state.basePose);
                        entry.state.events.clear();
                        entry.state.rootDelta = {};
                    }
                    else
                        Animator::Advance(*entry.component, entry.state, *entry.rig, seconds, Parameters(entity),
                                          &matrix);
                    if (!physical && entry.component->enabled &&
                        entry.state.rootMotionOverride.value_or(entry.component->rootMotion) &&
                        (entry.state.rootDelta.position != std::array<float, 3>{} ||
                         entry.state.rootDelta.rotation != std::array<float, 4>{0, 0, 0, 1}))
                    {
                        const auto before = *view.Transform(entity);
                        DirectX::XMFLOAT4X4 local, moved;
                        const auto delta = Animator::RootDeltaMatrix(*entry.component, entry.state, *entry.rig);
                        if (!SceneTransforms::Compose(before, local))
                            throw std::runtime_error("Invalid root motion transform");
                        DirectX::XMStoreFloat4x4(&moved,
                                                 DirectX::XMLoadFloat4x4(&delta) * DirectX::XMLoadFloat4x4(&local));
                        LocalTransform result;
                        std::string error;
                        if (!SceneTransforms::ReadTransform(moved, before, result) ||
                            !transaction.SetLocalTransform(entity, result, error).succeeded)
                            throw std::runtime_error("Root motion cannot be represented by object SRT");
                        roots.push_back({entity, before});
                    }
                    if (updating)
                        for (const auto& occurrence : entry.state.events)
                        {
                            const auto id = *view.ObjectId(entity);
                            ScriptEvent event{occurrence.name, id, id, occurrence.value};
                            event.animation = occurrence;
                            events.push_back(std::move(event));
                        }
                    if (entry.version == std::numeric_limits<std::uint64_t>::max())
                        throw std::runtime_error("Animation generations exhausted");
                    ++entry.version;
                    changed = true;
                }
                if (!roots.empty() && owner.services.constrain)
                {
                    std::string error;
                    if (!owner.services.constrain(transaction, roots, error))
                        throw std::runtime_error(error);
                }
                for (const auto& [key, value] : entries)
                {
                    static_cast<void>(value);
                    auto& entry = Write(key);
                    const auto ragdoll = transaction.Read().ComponentLease<RagdollComponent>(entry.instance.entity);
                    if (ragdoll && ragdoll->enabled && ragdoll->active)
                        continue;
                    const auto matrix = *transaction.Read().WorldMatrix(entry.instance.entity);
                    entry.state.pose = Animator::Constrain(*entry.component, entry.state, *entry.rig, &matrix);
                }
            }
            bool ReloadAssets(std::string&) override
            {
                Reconcile(true);
                return true;
            }
            bool BeforeUpdate(double seconds, const RuntimeInput& values, std::string&) override
            {
                input = values;
                Reconcile();
                const auto view = transaction.Read();
                for (const auto entity : view.With<RotatorComponent>())
                {
                    const auto rotator = view.ComponentLease<RotatorComponent>(entity);
                    if (!rotator->enabled)
                        continue;
                    auto pose = *view.Transform(entity);
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        const double speed = rotator->angularVelocity[axis];
                        if (speed == 0)
                            continue;
                        const auto elapsed = std::remainder(seconds, 360.0 / std::abs(speed));
                        pose.rotation[axis] = static_cast<float>(std::remainder(
                            static_cast<double>(pose.rotation[axis]) + elapsed * speed * DirectX::XM_PI / 180.0,
                            static_cast<double>(DirectX::XM_2PI)));
                    }
                    std::string error;
                    if (!transaction.SetLocalTransform(entity, pose, error).succeeded)
                        throw std::runtime_error(error);
                }
                return true;
            }
            bool Fixed(double, const RuntimeInput&, const RuntimeScriptEffects& effects, std::string&) override
            {
                Reconcile();
                for (const auto& [key, value] : entries)
                {
                    static_cast<void>(value);
                    auto& entry = Write(key);
                    Overrides(entry, effects);
                    RuntimeRagdoll::Synchronize(transaction, entry.instance.entity, *entry.rig,
                                                entry.state.basePose.empty() ? entry.state.pose : entry.state.basePose);
                }
                return true;
            }
            bool Update(double seconds, const RuntimeScriptEffects& effects, std::string&) override
            {
                updating = true;
                Evaluate(seconds, effects);
                return true;
            }
            bool Late(const RuntimeScriptEffects& effects, std::string&) override
            {
                Evaluate(0, effects);
                return true;
            }
            bool Prepare(const SceneChangeSet&, const RuntimeScriptEffects& effects, std::string&) override
            {
                Reconcile();
                for (const auto& [key, value] : entries)
                {
                    static_cast<void>(value);
                    auto& entry = Write(key);
                    Overrides(entry, effects);
                    const auto matrix = *transaction.Read().WorldMatrix(entry.instance.entity);
                    const auto view = transaction.Read();
                    const auto ragdoll = view.ComponentLease<RagdollComponent>(entry.instance.entity);
                    if (ragdoll && ragdoll->enabled && ragdoll->active && !ragdoll->bones.empty())
                        entry.state.pose = RuntimeRagdoll::Pose(view, entry.instance.entity, *entry.rig,
                                                                entry.state.basePose.empty() ? entry.state.pose
                                                                                             : entry.state.basePose);
                    else
                        entry.state.pose = Animator::Constrain(*entry.component, entry.state, *entry.rig, &matrix);
                    ++entry.version;
                    changed = true;
                }
                return true;
            }
            void Commit() noexcept override
            {
                owner.entries.swap(entries);
                owner.active = nullptr;
            }
            void Finish() noexcept override {}
            SceneChangeSet Changes() const override
            {
                SceneChangeSet result;
                if (changed)
                    for (const auto& [key, entry] : entries)
                    {
                        static_cast<void>(key);
                        result.fields.push_back({*transaction.Read().ObjectId(entry->instance.entity),
                                                 entry->component->id, "runtime.animator"});
                    }
                return result;
            }
            std::vector<ScriptEvent> TakeEvents() override
            {
                std::vector<ScriptEvent> result;
                result.swap(events);
                return result;
            }
        };
    };
    RuntimeAnimation::RuntimeAnimation(std::filesystem::path root, RuntimeAnimationServices services)
        : impl_(std::make_unique<Impl>())
    {
        impl_->root = std::move(root);
        impl_->services = std::move(services);
    }
    RuntimeAnimation::~RuntimeAnimation() = default;
    const AnimatorState* RuntimeAnimation::Status(ComponentInstanceHandle instance) const noexcept
    {
        const auto found = impl_->entries.find(instance.generation);
        return found != impl_->entries.end() && found->second->instance == instance ? &found->second->state : nullptr;
    }
    std::vector<RuntimeSkinPose> RuntimeAnimation::Snapshot() const
    {
        return Impl::Snapshot(impl_->entries);
    }
    std::vector<RuntimeSkinPose> RuntimeAnimation::CandidateSnapshot() const
    {
        return Impl::Snapshot(impl_->active ? impl_->active->entries : impl_->entries);
    }
    std::unique_ptr<RuntimeSystem::Frame> RuntimeAnimation::Begin(WorldState::Transaction& transaction)
    {
        return std::make_unique<Impl::Candidate>(*impl_, transaction);
    }
    RuntimeScriptQueries RuntimeAnimation::Queries() const
    {
        return {};
    }
}
