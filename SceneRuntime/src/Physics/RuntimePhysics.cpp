#include <SceneRuntime/Physics/RuntimePhysics.h>
#include <SceneRuntime/Physics/PhysicsWorld.h>

namespace SceneRuntime
{
    struct RuntimePhysics::Impl
    {
        PhysicsWorld physics;
        ScenePhysics::States states;
        RuntimePhysicalHistory history;
        float fixedStep = 0;
        std::map<std::string, ComponentInstanceHandle> controllers;
        struct RootOverride
        {
            ComponentInstanceHandle instance;
            std::shared_ptr<const AnimatorComponent> component;
            bool enabled = false;
        };
        std::map<std::string, RootOverride> roots;
        std::filesystem::path root;
        struct Candidate;
        Candidate* active = nullptr;
        struct Candidate final : RuntimeSystem::Frame
        {
            Impl& owner;
            WorldState::Transaction& transaction;
            PhysicsWorld::Transaction rollback;
            ScenePhysics::States states;
            std::map<std::string, ComponentInstanceHandle> controllers;
            std::map<std::string, RootOverride> roots;
            std::vector<ScriptEvent> events;
            size_t impulses = 0;
            RuntimePhysicalHistory history;
            float fixedStep = 0;
            bool stepped = false, impulsed = false, reloaded = false;
            Candidate(Impl& value, WorldState::Transaction& tx)
                : owner(value), transaction(tx), rollback(value.physics.BeginTransaction()), states(value.states),
                  controllers(value.controllers), roots(value.roots), history(value.history)
            {
                if (owner.active)
                    throw std::logic_error("Reentrant runtime physics frame");
                owner.active = this;
            }
            ~Candidate()
            {
                if (owner.active == this)
                    owner.active = nullptr;
            }
            bool ApplyImpulses(const RuntimeScriptEffects& effects, std::string& error)
            {
                while (impulses < effects.impulses.size())
                {
                    const auto& impulse = effects.impulses[impulses++];
                    if (!transaction.Read().Alive(impulse.entity) || !transaction.Read().Alive(impulse.body))
                        continue;
                    if (!owner.physics.AddImpulse(transaction.Read(), impulse.entity, impulse.value))
                    {
                        error = "Runtime impulse rejected";
                        return false;
                    }
                    impulsed |= impulse.value != std::array<float, 3>{};
                }
                return true;
            }
            static std::vector<RuntimePhysicalPose> Capture(WorldState::ReadView view)
            {
                std::vector<RuntimePhysicalPose> result;
                for (const auto entity : view.Entities())
                {
                    const auto body = view.ComponentLease<RigidBodyComponent>(entity);
                    const auto controller = view.ComponentLease<PlayerControllerComponent>(entity);
                    if (!(body && body->enabled && body->motion != "static") && !(controller && controller->enabled))
                        continue;
                    result.push_back(
                        {entity, view.Parent(entity).value_or(EntityHandle{}),
                         view.Instance<RigidBodyComponent>(entity).value_or(ComponentInstanceHandle{}),
                         view.Instance<PlayerControllerComponent>(entity).value_or(ComponentInstanceHandle{}),
                         *view.Transform(entity)});
                }
                return result;
            }
            void BeforeFixed() override
            {
                history.previous = Capture(transaction.Read());
            }
            bool Fixed(double seconds, const RuntimeInput& input, const RuntimeScriptEffects& effects,
                       std::string& error) override
            {
                SynchronizeControllers();
                SynchronizeRoots(effects);
                if (!ApplyImpulses(effects, error))
                    return false;
                std::map<std::string, bool> rootMotions;
                for (const auto& [id, overrideValue] : roots)
                    rootMotions.emplace(id, overrideValue.enabled);
                if (!owner.physics.AdvanceCandidate(transaction, states, seconds, input.horizontal, input.vertical,
                                                    input.jump, owner.root, error, rootMotions, rollback))
                    return false;
                for (const auto& contact : owner.physics.Events())
                {
                    const auto name = std::string(contact.trigger ? "trigger" : "collision") + contact.phase;
                    events.push_back({name, contact.first, contact.second});
                    events.push_back({name, contact.second, contact.first});
                }
                history.current = Capture(transaction.Read());
                stepped = true;
                return true;
            }
            bool Update(double, const RuntimeScriptEffects&, std::string&) override
            {
                return true;
            }
            bool ReloadAssets(std::string& error) override
            {
                reloaded = true;
                return owner.physics.PrepareScene(transaction.Read(), states, owner.root, error, true);
            }
            bool Prepare(const SceneChangeSet&, const RuntimeScriptEffects& effects, std::string& error) override
            {
                fixedStep = transaction.Read().Definition()->Source().settings.fixedStep;
                if (!stepped && owner.fixedStep != fixedStep)
                {
                    history.previous.clear();
                    history.current.clear();
                }
                SynchronizeControllers();
                SynchronizeRoots(effects);
                return ApplyImpulses(effects, error) &&
                       owner.physics.PrepareScene(transaction.Read(), states, owner.root, error);
            }
            void SynchronizeControllers()
            {
                const auto view = transaction.Read();
                std::map<std::string, ComponentInstanceHandle> current;
                for (const auto entity : view.With<PlayerControllerComponent>())
                {
                    const auto controller = view.ComponentLease<PlayerControllerComponent>(entity);
                    if (!controller->enabled)
                        continue;
                    const auto ragdoll = view.ComponentLease<RagdollComponent>(entity);
                    if (ragdoll && ragdoll->enabled && ragdoll->active && !ragdoll->bones.empty())
                        continue;
                    const auto id = *view.ObjectId(entity);
                    const auto instance = *view.Instance<PlayerControllerComponent>(entity);
                    current.emplace(id, instance);
                    if (!controllers.contains(id) || controllers.at(id) != instance)
                        states.erase(id);
                }
                std::erase_if(states,
                              [&](const auto& state)
                              {
                                  return !current.contains(state.first);
                              });
                controllers.swap(current);
            }
            void SynchronizeRoots(const RuntimeScriptEffects& effects)
            {
                const auto view = transaction.Read();
                std::erase_if(roots,
                              [&](const auto& entry)
                              {
                                  if (!view.Alive(entry.second.instance))
                                      return true;
                                  const auto component =
                                      view.ComponentLease<AnimatorComponent>(entry.second.instance.entity);
                                  return !component || *component != *entry.second.component;
                              });
                for (const auto& [id, enabled] : effects.rootMotions)
                {
                    const auto entity = view.Resolve(id);
                    const auto instance = entity ? view.Instance<AnimatorComponent>(*entity) : std::nullopt;
                    if (!instance || !enabled)
                    {
                        roots.erase(id);
                        continue;
                    }
                    roots[id] = {*instance, view.ComponentLease<AnimatorComponent>(*entity), *enabled};
                }
            }
            void Commit() noexcept override
            {
                owner.history.previous.swap(history.previous);
                owner.history.current.swap(history.current);
                owner.fixedStep = fixedStep;
                owner.states.swap(states);
                owner.controllers.swap(controllers);
                owner.roots.swap(roots);
                rollback.Commit();
                owner.active = nullptr;
            }
            void Finish() noexcept override {}
            SceneChangeSet Changes() const override
            {
                SceneChangeSet changes;
                if (stepped || impulsed || reloaded)
                    changes.fields.push_back({{}, {}, "runtime.physics"});
                return changes;
            }
            std::vector<ScriptEvent> TakeEvents() override
            {
                std::vector<ScriptEvent> result;
                result.swap(events);
                return result;
            }
        };
    };
    RuntimePhysics::RuntimePhysics(std::filesystem::path root) : impl_(std::make_unique<Impl>())
    {
        impl_->root = std::move(root);
    }
    RuntimePhysics::~RuntimePhysics() = default;
    const RuntimePhysicalHistory& RuntimePhysics::History() const noexcept
    {
        return impl_->active ? impl_->active->history : impl_->history;
    }
    std::unique_ptr<RuntimeSystem::Frame> RuntimePhysics::Begin(WorldState::Transaction& transaction)
    {
        return std::make_unique<Impl::Candidate>(*impl_, transaction);
    }
    RuntimeScriptQueries RuntimePhysics::Queries() const
    {
        return {[this](const std::array<float, 3>& origin, const std::array<float, 3>& direction, float distance,
                       unsigned int mask, bool triggers, const std::string& ignore)
                {
                    return impl_->physics.Raycast(origin, direction, distance, mask, triggers, ignore);
                }};
    }
    bool RuntimePhysics::Constrain(WorldState::Transaction& transaction, const std::vector<RuntimePose>& previous,
                                   std::string& error) const
    {
        return impl_->physics.ConstrainRootMotion(transaction, previous, impl_->root, error);
    }
    float RuntimePhysics::Grounded(WorldState::ReadView view, EntityHandle entity) const
    {
        const auto id = view.ObjectId(entity);
        const auto instance = view.Instance<PlayerControllerComponent>(entity);
        if (!id || !instance)
            return 1;
        const auto& controllers = impl_->active ? impl_->active->controllers : impl_->controllers;
        const auto& states = impl_->active ? impl_->active->states : impl_->states;
        const auto found = controllers.find(*id);
        const auto state = states.find(*id);
        return found == controllers.end() || found->second != *instance || state == states.end()
                   ? 1.f
                   : (state->second.grounded ? 1.f : 0.f);
    }
}
