#include <SceneRuntime/Scripts/RuntimeScripts.h>
#include <Engine/Core/Utf8.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>
#include <stdexcept>

namespace
{
    using namespace SceneRuntime;
    void Check(bool valid, const char* message)
    {
        if (!valid)
            throw std::runtime_error(message);
    }
    void CheckValue(const ScriptComponent& script, const RuntimeScriptDefinition& definition)
    {
        size_t remaining = 4096;
        ScriptValue::ValidateObject(script.data, 0, remaining);
        for (const auto& [name, value] : script.data)
        {
            const auto field = definition.dataFields.find(name);
            Check(field == definition.dataFields.end() || field->second.value.index() == value.value.index(),
                  "Incompatible script data field");
        }
        for (const auto& [name, field] : definition.fields)
        {
            const auto value = script.parameters.contains(name) ? script.parameters.at(name) : field.initial;
            Check(std::isfinite(value) && value >= field.minimum && value <= field.maximum,
                  "Script parameter outside range");
        }
    }
    void SetTransform(RuntimeScriptContext& context, const LocalTransform& value)
    {
        Check(context.SetLocalTransform(value).succeeded, "Invalid script transform");
    }
    RuntimeScriptDefinitions Builtins()
    {
        RuntimeScriptDefinitions result;
        for (const auto& [name, schema] : ScriptSchema::Builtins())
            static_cast<ScriptSchema&>(result[name]) = schema;
        result.at("Bob").start = [](RuntimeScriptContext& context)
        {
            context.state["origin"] = context.Read().Transform(context.entity)->position[1];
            context.state["time"] = 0;
        };
        result.at("Bob").update = [](RuntimeScriptContext& context)
        {
            context.state["time"] += static_cast<float>(context.seconds);
            auto value = *context.Read().Transform(context.entity);
            value.position[1] =
                context.state["origin"] + std::sin(context.state["time"] * context.Value("frequency", 1) * 6.2831853f) *
                                              context.Value("amplitude", .5f);
            SetTransform(context, value);
        };
        result.at("Spin").update = [](RuntimeScriptContext& context)
        {
            auto value = *context.Read().Transform(context.entity);
            value.rotation[1] = std::remainder(value.rotation[1] + static_cast<float>(context.seconds) *
                                                                       context.Value("speed", 90) * .0174532925f,
                                               6.2831853f);
            SetTransform(context, value);
        };
        auto& pulse = result.at("AnimationEventPulse");
        pulse.start = [](RuntimeScriptContext& context)
        {
            const auto light = context.Read().Component<PointLightComponent>(context.entity);
            context.state["origin"] = light ? light->intensity : 0;
            context.state["remaining"] = 0;
            context.state["scale"] = 1;
        };
        pulse.onEvent = [](RuntimeScriptContext& context)
        {
            if (!context.event || !context.event->animation)
                return;
            context.state["events"] += 1;
            context.state["remaining"] = context.Value("duration", .2f);
            context.state["scale"] =
                context.event->animation->weight * std::clamp(std::abs(context.event->value), .1f, 2.f);
        };
        pulse.update = [](RuntimeScriptContext& context)
        {
            context.state["remaining"] =
                std::max(0.f, context.state["remaining"] - static_cast<float>(context.seconds));
            auto light = context.Read().Component<PointLightComponent>(context.entity);
            if (light)
            {
                light->intensity = std::clamp(context.state["origin"] +
                                                  context.Value("peak", 15) * context.state["scale"] *
                                                      context.state["remaining"] / context.Value("duration", .2f),
                                              0.f, 10000.f);
                Check(context.SetComponent(std::move(light)).succeeded, "Invalid pulse light");
            }
        };
        pulse.prepareStop = [](RuntimeScriptContext& context)
        {
            auto light = context.Read().Component<PointLightComponent>(context.entity);
            if (light)
            {
                light->intensity = context.state["origin"];
                Check(context.SetComponent(std::move(light)).succeeded, "Invalid restored light");
            }
        };
        result.at("IkOrbit").update = [](RuntimeScriptContext& context)
        {
            const auto animator = context.Read().Component<AnimatorComponent>(context.entity);
            if (!animator || animator->ik.empty())
                return;
            const auto& item = animator->ik.front();
            auto& phase = context.state["phase"];
            phase = std::remainder(
                phase + static_cast<float>(context.seconds) * context.Value("frequency", .3f) * 6.2831853f, 6.2831853f);
            AnimatorIkTarget target{item.target, item.hint, context.Value("weight", 1), item.worldSpace};
            target.target[0] += std::sin(phase) * context.Value("radius", .25f);
            target.target[2] += std::cos(phase) * context.Value("radius", .25f);
            context.scene.SetIkTarget(context.entity, item.name, target);
        };
        result.at("RootMotionControl").update = [](RuntimeScriptContext& context)
        {
            if (context.Read().Component<AnimatorComponent>(context.entity))
            {
                const auto enabled = context.Value("enabled", 1);
                context.scene.SetRootMotion(context.entity,
                                            enabled < 0 ? std::nullopt : std::optional<bool>{enabled > 0});
            }
        };
        result.at("FollowTarget").update = [](RuntimeScriptContext& context)
        {
            if (!std::get<bool>(context.Data("active")->value))
                return;
            const auto target = context.Reference("target");
            if (!target)
                return;
            const auto view = context.Read();
            Check(view.Parent(*target) == view.Parent(context.entity), "FollowTarget requires the same parent");
            const auto& offset = std::get<ScriptValue::Object>(context.Data("offset")->value);
            const char* axes[] = {"x", "y", "z"};
            auto value = *view.Transform(context.entity);
            const auto position = view.Transform(*target)->position;
            for (size_t axis = 0; axis < 3; ++axis)
                value.position[axis] = position[axis] + std::get<float>(offset.at(axes[axis]).value);
            SetTransform(context, value);
        };
        return result;
    }
    struct Registry
    {
        std::mutex mutex;
        RuntimeScriptDefinitions definitions = Builtins();
        std::map<std::string, std::set<std::string>> owners;
        std::uint64_t revision = 0;
    };
    Registry& Registrations()
    {
        static Registry registry;
        return registry;
    }
    struct Instance
    {
        ComponentInstanceHandle handle;
        std::string behavior, objectId, componentId;
        std::map<std::string, float> parameters, state;
        ScriptValue::Object data, dataState;
        std::shared_ptr<const ScenePlacement> retiredOwner;
    };
    using Instances = std::map<std::uint64_t, std::shared_ptr<const Instance>>;
    struct Stopped
    {
        RuntimeScriptStopContext context;
        const RuntimeScriptDefinition* definition;
    };
    void Finish(const Stopped& stopped) noexcept
    {
        try
        {
            if (stopped.definition->stop)
                stopped.definition->stop(stopped.context);
        }
        catch (...)
        {
            // 確定済み状態を終了処理の例外で巻き戻さない。
        }
    }
}

namespace SceneRuntime
{
    struct RuntimeScriptScene::Impl
    {
        WorldState::Transaction& transaction;
        RuntimeScriptEffects& effects;
        std::vector<ScriptEvent>& events;
        RuntimeScriptQueries& queries;
        std::vector<std::pair<EntityHandle, RuntimeComponentValue>> values;
        std::optional<SceneLayout> structure;
        std::vector<std::pair<std::string, std::string>> duplicates;
        size_t nextId = 1;
        std::set<std::string> retiredIds;

        SceneEditPlan Edit(const SceneEdit& edit)
        {
            if (!structure)
            {
                FlushValues();
                structure = transaction.Read().ExportLayout();
            }
            std::string error;
            auto plan = SceneEdits::Prepare(*structure, edit, nextId, error);
            if (!plan)
                throw std::runtime_error(error);
            for (const auto& object : structure->objects)
                if (std::none_of(plan->layout.objects.begin(), plan->layout.objects.end(),
                                 [&](const auto& candidate)
                                 {
                                     return candidate.id == object.id;
                                 }))
                    retiredIds.insert(object.id);
            nextId = plan->nextId;
            duplicates.insert(duplicates.end(), plan->duplicates.begin(), plan->duplicates.end());
            structure = plan->layout;
            return std::move(*plan);
        }
        void FlushValues()
        {
            std::string error;
            for (auto& [entity, value] : values)
            {
                std::visit(
                    [&](auto& component)
                    {
                        using T = std::decay_t<decltype(component)>;
                        if constexpr (!std::is_same_v<T, std::monostate>)
                        {
                            SceneEditResult result;
                            if constexpr (requires { component.has_value(); })
                                result = transaction.SetComponent(entity, std::move(component), error);
                            else
                                result = transaction.SetRepeated(entity, std::move(component), error);
                            if (!result.succeeded)
                                throw std::runtime_error(error);
                        }
                    },
                    value);
            }
            values.clear();
        }
        void Flush()
        {
            FlushValues();
            if (structure)
            {
                std::string error;
                // 同一phaseで同じ保存IDを再利用しても、破棄前の実行ハンドルを引き継がない。
                std::vector<std::string> retired;
                for (const auto& id : retiredIds)
                    if (transaction.Read().Resolve(id))
                        retired.push_back(id);
                if (!retired.empty() && !transaction.ApplyStructure(RemoveObjectsEdit{retired}, error).succeeded)
                    throw std::runtime_error(error);
                if (!transaction
                         .ApplyStructure(ReplaceDocumentEdit{std::move(*structure), std::move(duplicates)}, error)
                         .succeeded)
                    throw std::runtime_error(error);
                structure.reset();
                duplicates.clear();
                retiredIds.clear();
            }
        }
        std::string Id(EntityHandle entity) const
        {
            const auto id = transaction.Read().ObjectId(entity);
            Check(id.has_value(), "Invalid script entity handle");
            return *id;
        }
    };

    RuntimeScriptContext::RuntimeScriptContext(EntityHandle owner, const std::map<std::string, float>& parametersValue,
                                               std::map<std::string, float>& stateValue,
                                               const ScriptValue::Object& dataValue,
                                               ScriptValue::Object& dataStateValue, RuntimeScriptScene& sceneValue)
        : entity(std::move(owner)), parameters(parametersValue), state(stateValue), data(dataValue),
          dataState(dataStateValue), scene(sceneValue)
    {
    }
    WorldState::ReadView RuntimeScriptContext::Read() const noexcept
    {
        return scene.Read();
    }
    const ScriptValue* RuntimeScriptContext::Data(const std::string& name) const
    {
        const auto found = data.find(name);
        return found == data.end() ? nullptr : &found->second;
    }
    std::optional<EntityHandle> RuntimeScriptContext::Reference(const std::string& name) const
    {
        const auto* value = Data(name);
        const auto* reference = value ? std::get_if<ScriptObjectReference>(&value->value) : nullptr;
        return reference ? scene.Find(reference->id) : std::nullopt;
    }
    float RuntimeScriptContext::Value(const std::string& name, float fallback) const
    {
        const auto found = parameters.find(name);
        return found == parameters.end() ? fallback : found->second;
    }
    float RuntimeScriptContext::Input(const std::string& name) const
    {
        if (!input_)
            return 0;
        const auto found = input_->find(name);
        return found == input_->end() ? 0 : found->second;
    }
    bool RuntimeScriptContext::Pressed(const std::string& name) const
    {
        return pressed_ && pressed_->contains(name) && pressed_->at(name);
    }
    SceneEditResult RuntimeScriptContext::SetLocalTransform(const LocalTransform& value)
    {
        std::string error;
        auto result = scene.impl_->transaction.SetLocalTransform(entity, value, error);
        if (result.succeeded && scene.impl_->structure)
        {
            const auto id = scene.impl_->Id(entity);
            if (std::any_of(scene.impl_->structure->objects.begin(), scene.impl_->structure->objects.end(),
                            [&](const auto& object)
                            {
                                return object.id == id;
                            }))
                scene.impl_->Edit(TransformEdit{id, value});
        }
        return result;
    }
    void RuntimeScriptContext::SetParent(EntityHandle parent)
    {
        scene.SetParent(entity, std::move(parent));
    }

    RuntimeScriptScene::RuntimeScriptScene(Impl& impl) noexcept : impl_(&impl) {}
    WorldState::ReadView RuntimeScriptScene::Read() const noexcept
    {
        return impl_->transaction.Read();
    }
    std::optional<EntityHandle> RuntimeScriptScene::Find(std::string_view id) const
    {
        return Read().Resolve(id);
    }
    std::vector<EntityHandle> RuntimeScriptScene::FindByName(std::string_view name) const
    {
        return Read().FindByName(name);
    }
    std::string RuntimeScriptScene::Spawn(ScenePlacement value)
    {
        auto plan = impl_->Edit(AddObjectEdit{std::move(value)});
        return plan.createdIds.at(0);
    }
    std::string RuntimeScriptScene::Instantiate(EntityHandle entity, const std::array<float, 3>& position)
    {
        const auto transform = Read().Transform(entity);
        Check(transform.has_value(), "Cannot instantiate a stale entity");
        std::array<float, 3> offset{};
        for (size_t axis = 0; axis < 3; ++axis)
            offset[axis] = position[axis] - transform->position[axis];
        return impl_->Edit(DuplicateEdit{{impl_->Id(entity)}, offset}).createdIds.at(0);
    }
    void RuntimeScriptScene::Destroy(EntityHandle entity)
    {
        impl_->Edit(RemoveObjectsEdit{{impl_->Id(entity)}});
    }
    void RuntimeScriptScene::SetParent(EntityHandle entity, EntityHandle parent)
    {
        impl_->Edit(ParentEdit{{impl_->Id(entity)}, parent ? impl_->Id(parent) : std::string{}});
    }
    SceneEditResult RuntimeScriptScene::SetValue(EntityHandle entity, RuntimeComponentValue value)
    {
        if (!Read().Alive(entity))
            return {};
        if (impl_->structure)
        {
            const auto id = impl_->Id(entity);
            auto object = std::find_if(impl_->structure->objects.begin(), impl_->structure->objects.end(),
                                       [&](const auto& item)
                                       {
                                           return item.id == id;
                                       });
            if (object == impl_->structure->objects.end())
                return {true, {}};
            std::visit(
                [&](auto& component)
                {
#define ENGINE_SET_OPTIONAL(Type, member, references, assets, order)                                                   \
    if constexpr (std::is_same_v<std::decay_t<decltype(component)>, std::optional<Type>>)                              \
        object->member = component;
#define ENGINE_SET_REPEATED(Type, member, references, assets, order)                                                   \
    if constexpr (std::is_same_v<std::decay_t<decltype(component)>, std::vector<Type>>)                                \
        object->member = component;
                    ENGINE_SCENE_COMPONENTS(ENGINE_SET_OPTIONAL, ENGINE_SET_REPEATED)
#undef ENGINE_SET_OPTIONAL
#undef ENGINE_SET_REPEATED
                },
                value);
        }
        else
            impl_->values.emplace_back(std::move(entity), std::move(value));
        return {true, {}};
    }
    void RuntimeScriptScene::Emit(ScriptEvent event)
    {
        if (event.animation)
            AnimationEvents::ValidateOccurrence(*event.animation);
        Check(!event.name.empty() && event.name.size() <= 128 && event.sender.size() <= 128 &&
                  event.target.size() <= 128 && event.text.size() <= 4096 && std::isfinite(event.value) &&
                  impl_->events.size() < 4096,
              "Invalid script event or queue limit");
        impl_->events.push_back(std::move(event));
    }
    ScenePlacement RuntimeScriptScene::LegacyComponents(EntityHandle entity) const
    {
        auto object = Read().Object(entity);
        Check(object.has_value(), "Legacy component owner missing");
        if (impl_->structure)
            for (const auto& candidate : impl_->structure->objects)
                if (candidate.id == object->id)
                    object->CopyComponents(candidate);
        for (const auto& [owner, value] : impl_->values)
            if (owner == entity)
                std::visit(
                    [&](const auto& component)
                    {
#define ENGINE_LEGACY_OVERLAY_OPTIONAL(Type, member, references, assets, order)                                        \
    if constexpr (std::is_same_v<std::decay_t<decltype(component)>, std::optional<Type>>)                              \
        object->member = component;
#define ENGINE_LEGACY_OVERLAY_REPEATED(Type, member, references, assets, order)                                        \
    if constexpr (std::is_same_v<std::decay_t<decltype(component)>, std::vector<Type>>)                                \
        object->member = component;
                        ENGINE_SCENE_COMPONENTS(ENGINE_LEGACY_OVERLAY_OPTIONAL, ENGINE_LEGACY_OVERLAY_REPEATED)
#undef ENGINE_LEGACY_OVERLAY_OPTIONAL
#undef ENGINE_LEGACY_OVERLAY_REPEATED
                    },
                    value);
        return std::move(*object);
    }
    void RuntimeScriptScene::SetLegacyComponents(EntityHandle entity, const ScenePlacement& value)
    {
        const auto before = LegacyComponents(entity);
#define ENGINE_LEGACY_SET(Type, member, references, assets, order)                                                     \
    if (before.member != value.member)                                                                                 \
        Check(SetValue(entity, RuntimeComponentValue{value.member}).succeeded, "Legacy component request failed");
        ENGINE_SCENE_COMPONENTS(ENGINE_LEGACY_SET, ENGINE_LEGACY_SET)
#undef ENGINE_LEGACY_SET
    }
    void RuntimeScriptScene::ApplyLegacyObject(EntityHandle entity, const ScenePlacement& before,
                                               const ScenePlacement& after)
    {
        const auto stable = [](const auto& left, const auto& right)
        {
            if constexpr (requires { left.has_value(); })
                return left.has_value() == right.has_value() && (!left || left->id == right->id);
            else
                return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
                                                                 [](const auto& a, const auto& b)
                                                                 {
                                                                     return a.id == b.id;
                                                                 });
        };
        const auto apply = [&](const auto& left, const auto& right)
        {
            if (left == right)
                return;
            if (!stable(left, right))
            {
                Check(SetValue(entity, RuntimeComponentValue{right}).succeeded,
                      "Legacy structural component request failed");
                return;
            }
            std::string error;
            SceneEditResult result;
            if constexpr (requires { right.has_value(); })
                result = impl_->transaction.SetComponent(entity, right, error);
            else
                result = impl_->transaction.SetRepeated(entity, right, error);
            if (!result.succeeded)
                throw std::runtime_error(error);
            if (impl_->structure)
                Check(SetValue(entity, RuntimeComponentValue{right}).succeeded, "Legacy component projection failed");
        };
#define ENGINE_LEGACY_APPLY(Type, member, references, assets, order) apply(before.member, after.member);
        ENGINE_SCENE_COMPONENTS(ENGINE_LEGACY_APPLY, ENGINE_LEGACY_APPLY)
#undef ENGINE_LEGACY_APPLY
        if (before.name != after.name || before.parentId != after.parentId || before.persistent != after.persistent ||
            before.prefab != after.prefab)
        {
            if (!impl_->structure)
            {
                impl_->FlushValues();
                impl_->structure = impl_->transaction.Read().ExportLayout();
            }
            const auto found = std::find_if(impl_->structure->objects.begin(), impl_->structure->objects.end(),
                                            [&](const auto& item)
                                            {
                                                return item.id == after.id;
                                            });
            if (found == impl_->structure->objects.end())
                return;
            found->name = after.name;
            found->parentId = after.parentId;
            found->persistent = after.persistent;
            found->prefab = after.prefab;
        }
    }
    void RuntimeScriptScene::LoadScene(std::string scene, bool additive)
    {
        Check(!scene.empty() && scene.size() <= 4096 && impl_->effects.scenes.size() < 256, "Invalid scene command");
        impl_->effects.scenes.push_back({std::move(scene), additive, false});
    }
    void RuntimeScriptScene::UnloadScene(std::string scene)
    {
        LoadScene(std::move(scene));
        impl_->effects.scenes.back().unload = true;
    }
    void RuntimeScriptScene::SetUiValue(std::string key, float value)
    {
        Check(!key.empty() && key.size() <= 128 && key.find_first_of("=&\0", 0, 3) == std::string::npos &&
                  std::isfinite(value) && std::abs(value) <= 100000 &&
                  (impl_->effects.ui.values.contains(key) ||
                   impl_->effects.ui.values.size() + impl_->effects.ui.texts.size() < 256),
              "Invalid UI value");
        impl_->effects.ui.values[std::move(key)] = value;
    }
    void RuntimeScriptScene::SetUiText(std::string key, std::string text)
    {
        Check(!key.empty() && key.size() <= 128 && key.find_first_of("=&\0", 0, 3) == std::string::npos &&
                  text.size() <= 4096 && text.find('\0') == std::string::npos && Engine::ValidUtf8(text) &&
                  (impl_->effects.ui.texts.contains(key) ||
                   impl_->effects.ui.values.size() + impl_->effects.ui.texts.size() < 256),
              "Invalid UI text");
        impl_->effects.ui.texts[std::move(key)] = std::move(text);
    }
    void RuntimeScriptScene::PlayAudio(const std::string& object)
    {
        AudioCommand command{AudioCommandKind::Play, object};
        command.Validate();
        const auto entity = Find(object);
        Check(impl_->effects.audio.size() < 64 && entity && Read().ComponentLease<AudioSourceComponent>(*entity),
              "Audio source missing or command limit exceeded");
        impl_->effects.audio.push_back(std::move(command));
    }
    void RuntimeScriptScene::AudioCue(const std::string& cue)
    {
        AudioCommand command{AudioCommandKind::Cue, cue};
        command.Validate();
        Check(impl_->effects.audio.size() < 64, "Audio command limit exceeded");
        impl_->effects.audio.push_back(std::move(command));
    }
    void RuntimeScriptScene::AddImpulse(EntityHandle entity, const std::array<float, 3>& value)
    {
        const auto body = Read().ComponentLease<RigidBodyComponent>(entity);
        Check(body && body->enabled && body->motion == "dynamic", "Impulse requires a dynamic rigid body");
        Check(Read().Alive(entity) &&
                  std::all_of(value.begin(), value.end(),
                              [](float item)
                              {
                                  return std::isfinite(item);
                              }) &&
                  impl_->effects.impulses.size() < 4096,
              "Invalid impulse");
        const auto instance = *Read().Instance<RigidBodyComponent>(entity);
        auto sum = value;
        for (const auto& previous : impl_->effects.impulses)
            if (previous.body == instance)
                for (size_t axis = 0; axis < 3; ++axis)
                    sum[axis] += previous.value[axis];
        Check(std::all_of(sum.begin(), sum.end(),
                          [](float number)
                          {
                              return std::isfinite(number) && std::abs(number) <= 1000000;
                          }),
              "Invalid aggregate impulse");
        impl_->effects.impulses.push_back({std::move(entity), value, instance});
    }
    void RuntimeScriptScene::SetAnimatorParameter(EntityHandle entity, std::string name, float value)
    {
        Check(static_cast<bool>(Read().ComponentLease<AnimatorComponent>(entity)), "Animator parameter owner missing");
        const auto id = impl_->Id(entity);
        const auto existing = impl_->effects.animatorParameters.find(id);
        auto values =
            existing == impl_->effects.animatorParameters.end() ? std::map<std::string, float>{} : existing->second;
        values[name] = value;
        Animator::ValidateParameters(values);
        impl_->effects.animatorParameters[id] = std::move(values);
        if (const auto clear = impl_->effects.clearAnimatorParameters.find(id);
            clear != impl_->effects.clearAnimatorParameters.end())
            std::erase(clear->second, name);
    }
    void RuntimeScriptScene::ClearAnimatorParameter(EntityHandle entity, std::string name)
    {
        Check(static_cast<bool>(Read().ComponentLease<AnimatorComponent>(entity)), "Animator parameter owner missing");
        Animator::ValidateParameters({{name, 0.f}});
        const auto id = impl_->Id(entity);
        auto& clear = impl_->effects.clearAnimatorParameters[id];
        if (std::find(clear.begin(), clear.end(), name) == clear.end())
        {
            Check(clear.size() < 64, "Animator clear command limit exceeded");
            clear.push_back(name);
        }
        if (const auto values = impl_->effects.animatorParameters.find(id);
            values != impl_->effects.animatorParameters.end())
            values->second.erase(name);
    }
    void RuntimeScriptScene::SetIkTarget(EntityHandle entity, std::string name, std::optional<AnimatorIkTarget> value)
    {
        const auto animator = Read().ComponentLease<AnimatorComponent>(entity);
        Check(animator && std::any_of(animator->ik.begin(), animator->ik.end(),
                                      [&](const auto& item)
                                      {
                                          return item.name == name;
                                      }),
              "IK constraint owner missing");
        if (value)
            Animator::ValidateIkTarget(*value);
        impl_->effects.ikTargets[impl_->Id(entity)][std::move(name)] = std::move(value);
    }
    void RuntimeScriptScene::SetRootMotion(EntityHandle entity, std::optional<bool> enabled)
    {
        const auto animator = Read().ComponentLease<AnimatorComponent>(entity);
        Check(animator && (!enabled.value_or(false) || !animator->rootBone.empty()),
              "Root motion owner or bone missing");
        impl_->effects.rootMotions[impl_->Id(entity)] = enabled;
    }
    std::optional<PhysicsRayHit> RuntimeScriptScene::Raycast(const std::array<float, 3>& origin,
                                                             const std::array<float, 3>& direction, float distance,
                                                             unsigned int mask, bool triggers,
                                                             const std::string& ignore) const
    {
        return impl_->queries.raycast ? impl_->queries.raycast(origin, direction, distance, mask, triggers, ignore)
                                      : std::nullopt;
    }

    void RuntimeScriptEffects::Swap(RuntimeScriptEffects& other) noexcept
    {
        scenes.swap(other.scenes);
        ui.values.swap(other.ui.values);
        ui.texts.swap(other.ui.texts);
        audio.swap(other.audio);
        impulses.swap(other.impulses);
        animatorParameters.swap(other.animatorParameters);
        clearAnimatorParameters.swap(other.clearAnimatorParameters);
        ikTargets.swap(other.ikTargets);
        rootMotions.swap(other.rootMotions);
    }
    bool RuntimeScriptRegistry::Register(std::string name, RuntimeScriptDefinition definition)
    {
        try
        {
            definition.Validate(name);
            auto& registry = Registrations();
            std::lock_guard lock(registry.mutex);
            const bool added = registry.definitions.emplace(std::move(name), std::move(definition)).second;
            if (added)
                ++registry.revision;
            return added;
        }
        catch (...)
        {
            return false;
        }
    }
    void RuntimeScriptEffects::Clear() noexcept
    {
        scenes.clear();
        ui.values.clear();
        ui.texts.clear();
        audio.clear();
        impulses.clear();
        animatorParameters.clear();
        clearAnimatorParameters.clear();
        ikTargets.clear();
        rootMotions.clear();
    }
    std::shared_ptr<const RuntimeScriptDefinitions> RuntimeScriptRegistry::Capture()
    {
        auto& registry = Registrations();
        std::lock_guard lock(registry.mutex);
        return std::make_shared<const RuntimeScriptDefinitions>(registry.definitions);
    }
    std::uint64_t RuntimeScriptRegistry::Revision()
    {
        auto& registry = Registrations();
        std::lock_guard lock(registry.mutex);
        return registry.revision;
    }
    bool RuntimeScriptRegistry::InstallModule(const std::string& owner, RuntimeScriptDefinitions definitions,
                                              std::string& error)
    {
        try
        {
            Check(!owner.empty(), "Missing module owner");
            for (const auto& [name, definition] : definitions)
                definition.Validate(name);
            auto& registry = Registrations();
            std::lock_guard lock(registry.mutex);
            auto next = registry.definitions;
            auto owners = registry.owners;
            for (const auto& name : owners[owner])
                next.erase(name);
            owners[owner].clear();
            for (auto& [name, definition] : definitions)
            {
                Check(!next.contains(name), "Duplicate script behavior");
                owners[owner].insert(name);
                next.emplace(name, std::move(definition));
            }
            registry.definitions.swap(next);
            registry.owners.swap(owners);
            ++registry.revision;
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }

    struct RuntimeScripts::Impl
    {
        std::shared_ptr<const RuntimeScriptDefinitions> definitions;
        Instances instances;
        std::vector<ScriptEvent> events;
        RuntimeScriptEffects effects;
        std::shared_ptr<const Engine::ContentContext> content;
        std::filesystem::path root;
        bool active = false, paused = false;
    };
    struct RuntimeScripts::Frame::Pending
    {
        Impl& owner;
        WorldState::Transaction& transaction;
        Instances instances;
        std::set<std::uint64_t> writable, uninvoked;
        std::map<std::string, float> input;
        std::map<std::string, bool> pressed;
        std::vector<ScriptEvent> events;
        RuntimeScriptEffects effects;
        RuntimeScriptQueries queries;
        std::vector<Stopped> stops, rollbacks;
        std::map<std::uint64_t, ScenePlacement> retiringOwners;
        SceneChangeSet changes;
        bool paused, prepared = false, committed = false, finished = false, failed = false;

        Pending(Impl& value, WorldState::Transaction& tx)
            : owner(value), transaction(tx), instances(value.instances), events(value.events), effects(value.effects),
              paused(value.paused)
        {
            effects.impulses.clear();
            effects.animatorParameters.clear();
            effects.clearAnimatorParameters.clear();
            effects.ikTargets.clear();
            effects.rootMotions.clear();
        }
        Instance& Write(std::uint64_t key)
        {
            auto& value = instances.at(key);
            if (writable.insert(key).second)
                value = std::make_shared<Instance>(*value);
            return const_cast<Instance&>(*value);
        }
        RuntimeScriptContext Context(Instance& instance, RuntimeScriptScene& scene, ScriptPhase phase, double seconds)
        {
            RuntimeScriptContext context(instance.handle.entity, instance.parameters, instance.state, instance.data,
                                         instance.dataState, scene);
            context.phase = phase;
            context.seconds = std::min(seconds, .1);
            context.contentRoot = owner.root;
            context.content = owner.content;
            context.input_ = &input;
            context.pressed_ = &pressed;
            return context;
        }
        Stopped Snapshot(const Instance& instance)
        {
            auto object = transaction.Read().Object(instance.handle.entity);
            if (!object && instance.retiredOwner)
                object = *instance.retiredOwner;
            if (!object)
            {
                const auto found = retiringOwners.find(instance.handle.generation);
                if (found != retiringOwners.end())
                    object = found->second;
            }
            return {{instance.handle.entity, object.value_or(ScenePlacement{}), instance.parameters, instance.state,
                     instance.data, instance.dataState, owner.root, owner.content},
                    &owner.definitions->at(instance.behavior)};
        }
        void MarkInvoked(std::uint64_t key, const Instance& instance)
        {
            // 未公開でコールバック未実行の候補は、失敗時の停止対象にしません。
            if (uninvoked.erase(key))
                rollbacks.push_back(Snapshot(instance));
        }
        void Retire(std::uint64_t key, RuntimeScriptScene& scene)
        {
            auto& instance = Write(key);
            const auto& definition = owner.definitions->at(instance.behavior);
            const bool neverInvoked = uninvoked.erase(key) != 0;
            if (!neverInvoked && definition.prepareStop)
            {
                auto context = Context(instance, scene, ScriptPhase::Update, 0);
                ScenePlacement removed;
                if (!transaction.Read().Alive(instance.handle.entity))
                {
                    removed = Snapshot(instance).context.object;
                    context.retiredOwner_ = &removed;
                }
                definition.prepareStop(context);
                if (context.retiredOwner_)
                    instance.retiredOwner = std::make_shared<const ScenePlacement>(std::move(removed));
            }
            if (!neverInvoked)
                stops.push_back(Snapshot(instance));
            instances.erase(key);
            writable.erase(key);
        }
        void CaptureRetired()
        {
            std::vector<std::uint64_t> removed;
            for (const auto& [key, instance] : instances)
                if (!transaction.Read().Alive(instance->handle.entity) && !instance->retiredOwner)
                    removed.push_back(key);
            for (const auto key : removed)
            {
                const auto found = retiringOwners.find(key);
                if (found != retiringOwners.end())
                    Write(key).retiredOwner = std::make_shared<const ScenePlacement>(found->second);
            }
        }
        void Advance(ScriptPhase phase, double seconds, bool startOnly, std::optional<bool> pauseValue)
        {
            Check(!failed && !prepared && !committed && std::isfinite(seconds) && seconds >= 0,
                  "Invalid script phase state");
            RuntimeScriptScene::Impl sceneState{transaction, effects, events, queries, {}, {}, {}, 1, {}};
            RuntimeScriptScene scene(sceneState);
            std::vector<ScriptEvent> incoming;
            if (!startOnly && !pauseValue && phase == ScriptPhase::Update)
                incoming.swap(events);
            std::set<std::uint64_t> active;
            const auto view = transaction.Read();
            for (const auto entity : view.WithRepeated<ScriptComponent>())
            {
                const auto scripts = view.RepeatedLease<ScriptComponent>(entity);
                for (const auto& script : *scripts)
                {
                    if (!script.enabled)
                        continue;
                    const auto found = owner.definitions->find(script.behaviour);
                    Check(found != owner.definitions->end(), "Unknown script behavior");
                    const auto& definition = found->second;
                    CheckValue(script, definition);
                    const auto handle = *view.RepeatedInstance<ScriptComponent>(entity, script.id);
                    const auto key = handle.generation;
                    active.insert(key);
                    if (instances.contains(key) &&
                        (instances.at(key)->behavior != script.behaviour || instances.at(key)->handle != handle))
                        Retire(key, scene);
                    const bool starting = !instances.contains(key);
                    if (starting)
                    {
                        auto instance = std::make_shared<Instance>();
                        instance->handle = handle;
                        instance->behavior = script.behaviour;
                        instance->objectId = *view.ObjectId(entity);
                        instance->componentId = script.id;
                        instances.emplace(key, std::move(instance));
                        writable.insert(key);
                        if (definition.stop)
                            uninvoked.insert(key);
                    }
                    auto& instance = Write(key);
                    instance.parameters = script.parameters;
                    for (const auto& [name, field] : definition.fields)
                        instance.parameters.try_emplace(name, field.initial);
                    instance.data = definition.dataFields;
                    for (const auto& [name, value] : script.data)
                        instance.data[name] = value;
                    auto context = Context(instance, scene, phase, seconds);
                    if (starting)
                    {
                        try
                        {
                            if (definition.start)
                                definition.start(context);
                        }
                        catch (...)
                        {
                            Finish(Snapshot(instance));
                            throw;
                        }
                        if (definition.start)
                            MarkInvoked(key, instance);
                        if (paused && definition.pause)
                        {
                            MarkInvoked(key, instance);
                            definition.pause(context, true);
                        }
                    }
                    if (pauseValue)
                    {
                        if (definition.pause)
                        {
                            MarkInvoked(key, instance);
                            definition.pause(context, *pauseValue);
                        }
                    }
                    else if (!startOnly && !paused)
                    {
                        if (phase == ScriptPhase::Update && definition.onEvent)
                            for (const auto& event : incoming)
                                if (event.target.empty() || event.target == *view.ObjectId(entity))
                                {
                                    context.event = &event;
                                    MarkInvoked(key, instance);
                                    definition.onEvent(context);
                                }
                        context.event = nullptr;
                        const auto& callback = phase == ScriptPhase::Update        ? definition.update
                                               : phase == ScriptPhase::FixedUpdate ? definition.fixedUpdate
                                                                                   : definition.lateUpdate;
                        if (callback)
                        {
                            MarkInvoked(key, instance);
                            callback(context);
                        }
                    }
                    size_t remaining = 4096;
                    ScriptValue::ValidateObject(instance.dataState, 0, remaining);
                    for (const auto& [name, value] : instance.state)
                    {
                        static_cast<void>(name);
                        Check(std::isfinite(value), "Invalid script state number");
                    }
                }
            }
            std::vector<std::uint64_t> inactive;
            for (const auto& [key, instance] : instances)
            {
                static_cast<void>(instance);
                if (!active.contains(key))
                    inactive.push_back(key);
            }
            for (const auto key : inactive)
                Retire(key, scene);
            if (sceneState.structure)
                for (const auto& [key, instance] : instances)
                    if (const auto object = view.Object(instance->handle.entity))
                        retiringOwners[key] = *object;
            const bool hadStructure = sceneState.structure.has_value();
            sceneState.Flush();
            if (hadStructure)
            {
                CaptureRetired();
                std::vector<std::uint64_t> destroyed;
                for (const auto& [key, instance] : instances)
                    if (!transaction.Read().Alive(instance->handle.entity))
                        destroyed.push_back(key);
                for (const auto key : destroyed)
                    Retire(key, scene);
                sceneState.Flush();
            }
            if (pauseValue)
                paused = *pauseValue;
        }
    };
    RuntimeScripts::RuntimeScripts(std::shared_ptr<const RuntimeScriptDefinitions> definitions)
        : impl_(std::make_unique<Impl>())
    {
        Check(static_cast<bool>(definitions), "Missing script definitions");
        impl_->definitions = std::move(definitions);
    }
    RuntimeScripts::~RuntimeScripts() = default;
    RuntimeScripts::Frame::Frame(RuntimeScripts& owner, WorldState::Transaction& transaction,
                                 const std::map<std::string, float>& input, const std::map<std::string, bool>& pressed,
                                 RuntimeScriptQueries queries)
    {
        Check(!owner.impl_->active, "Reentrant script frame");
        pending_ = std::make_unique<Pending>(*owner.impl_, transaction);
        pending_->input = input;
        pending_->pressed = pressed;
        pending_->queries = std::move(queries);
        owner.impl_->active = true;
    }
    RuntimeScripts::Frame::~Frame()
    {
        if (!pending_->committed)
            for (const auto& stopped : pending_->rollbacks)
                Finish(stopped);
        pending_->owner.active = false;
    }
    bool RuntimeScripts::Frame::Start(std::string& error)
    {
        try
        {
            pending_->Advance(ScriptPhase::Update, 0, true, {});
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            pending_->failed = true;
            error = failure.what();
            return false;
        }
        catch (...)
        {
            pending_->failed = true;
            error = "Script preparation failed with an unknown exception";
            return false;
        }
    }
    bool RuntimeScripts::Frame::Edit(const SceneEdit& edit, std::string& error)
    {
        try
        {
            Check(!pending_->prepared && !pending_->committed && !pending_->failed,
                  "Cannot edit a prepared script frame");
            if (const auto* transform = std::get_if<TransformEdit>(&edit))
            {
                const auto entity = pending_->transaction.Read().Resolve(transform->id);
                if (!entity)
                {
                    pending_->failed = true;
                    error = "Transform owner missing";
                    return false;
                }
                const auto result = pending_->transaction.SetLocalTransform(*entity, transform->value, error);
                pending_->failed = !result.succeeded;
                return result.succeeded;
            }
            for (const auto& [key, instance] : pending_->instances)
                if (const auto object = pending_->transaction.Read().Object(instance->handle.entity))
                    pending_->retiringOwners[key] = *object;
            if (!pending_->transaction.ApplyStructure(edit, error).succeeded)
            {
                pending_->failed = true;
                return false;
            }
            pending_->CaptureRetired();
            return true;
        }
        catch (const std::exception& failure)
        {
            pending_->failed = true;
            error = failure.what();
            return false;
        }
        catch (...)
        {
            pending_->failed = true;
            error = "Script structural preparation failed";
            return false;
        }
    }
    bool RuntimeScripts::Frame::Animate(const std::map<std::string, float>& clocks, std::string& error, bool& changed)
    {
        changed = false;
        try
        {
            Check(!pending_->prepared && !pending_->committed && !pending_->failed,
                  "Cannot animate prepared runtime frame");
            const auto view = pending_->transaction.Read();
            for (const auto entity : view.With<AnimationComponent>())
            {
                const auto animation = view.ComponentLease<AnimationComponent>(entity);
                if (!animation->enabled || std::none_of(animation->tracks.begin(), animation->tracks.end(),
                                                        [](const auto& track)
                                                        {
                                                            return !track.property.starts_with("ui") &&
                                                                   track.property != "opacity";
                                                        }))
                    continue;
                ScenePlacement before;
                const auto local = *view.Transform(entity);
                before.position = local.position;
                before.rotation = local.rotation;
                before.scale = local.scale;
                before.material = view.Component<MaterialComponent>(entity);
                before.camera = view.Component<CameraComponent>(entity);
                before.directionalLight = view.Component<DirectionalLightComponent>(entity);
                before.pointLight = view.Component<PointLightComponent>(entity);
                before.spotLight = view.Component<SpotLightComponent>(entity);
                before.sky = view.Component<SkyComponent>(entity);
                before.particleEmitter = view.Component<ParticleEmitterComponent>(entity);
                auto after = before;
                for (const auto& track : animation->tracks)
                {
                    if (track.property.starts_with("ui") || track.property == "opacity")
                        continue;
                    const auto clock = clocks.find(track.clock);
                    if (clock == clocks.end() || clock->second < track.delay)
                        continue;
                    Check(std::isfinite(clock->second), "Invalid runtime animation clock");
                    if (const auto value = Animation::Sample(track, clocks))
                        Animation::Apply(after, track.property, *value);
                }
                if (after.position != before.position || after.rotation != before.rotation ||
                    after.scale != before.scale)
                {
                    changed = true;
                    Check(pending_->transaction
                              .SetLocalTransform(entity, {after.position, after.rotation, after.scale}, error)
                              .succeeded,
                          "Invalid animated runtime transform");
                }
                if (before.material != after.material)
                {
                    changed = true;
                    Check(pending_->transaction.SetComponent(entity, std::move(after.material), error).succeeded,
                          "Invalid animated material");
                }
                if (before.camera != after.camera)
                {
                    changed = true;
                    Check(pending_->transaction.SetComponent(entity, std::move(after.camera), error).succeeded,
                          "Invalid animated camera");
                }
                if (before.directionalLight != after.directionalLight)
                {
                    changed = true;
                    Check(
                        pending_->transaction.SetComponent(entity, std::move(after.directionalLight), error).succeeded,
                        "Invalid animated directionalLight");
                }
                if (before.pointLight != after.pointLight)
                {
                    changed = true;
                    Check(pending_->transaction.SetComponent(entity, std::move(after.pointLight), error).succeeded,
                          "Invalid animated pointLight");
                }
                if (before.spotLight != after.spotLight)
                {
                    changed = true;
                    Check(pending_->transaction.SetComponent(entity, std::move(after.spotLight), error).succeeded,
                          "Invalid animated spotLight");
                }
                if (before.sky != after.sky)
                {
                    changed = true;
                    Check(pending_->transaction.SetComponent(entity, std::move(after.sky), error).succeeded,
                          "Invalid animated sky");
                }
                if (before.particleEmitter != after.particleEmitter)
                {
                    changed = true;
                    Check(pending_->transaction.SetComponent(entity, std::move(after.particleEmitter), error).succeeded,
                          "Invalid animated particleEmitter");
                }
            }
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            pending_->failed = true;
            error = failure.what();
            return false;
        }
        catch (...)
        {
            pending_->failed = true;
            error = "Runtime animation properties failed";
            return false;
        }
    }
    bool RuntimeScripts::Frame::Run(ScriptPhase phase, double seconds, std::string& error)
    {
        try
        {
            pending_->Advance(phase, seconds, false, {});
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            pending_->failed = true;
            error = failure.what();
            return false;
        }
        catch (...)
        {
            pending_->failed = true;
            error = "Script preparation failed with an unknown exception";
            return false;
        }
    }
    bool RuntimeScripts::Frame::Pause(bool paused, std::string& error)
    {
        try
        {
            pending_->Advance(ScriptPhase::Update, 0, true, paused);
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            pending_->failed = true;
            error = failure.what();
            return false;
        }
        catch (...)
        {
            pending_->failed = true;
            error = "Script preparation failed with an unknown exception";
            return false;
        }
    }
    bool RuntimeScripts::Frame::HasPhase(ScriptPhase phase) const
    {
        const auto view = pending_->transaction.Read();
        for (const auto entity : view.WithRepeated<ScriptComponent>())
        {
            const auto scripts = view.RepeatedLease<ScriptComponent>(entity);
            for (const auto& script : *scripts)
            {
                if (!script.enabled)
                    continue;
                const auto found = pending_->owner.definitions->find(script.behaviour);
                if (found == pending_->owner.definitions->end())
                    continue;
                const auto& definition = found->second;
                if (phase == ScriptPhase::FixedUpdate ? static_cast<bool>(definition.fixedUpdate)
                    : phase == ScriptPhase::LateUpdate
                        ? static_cast<bool>(definition.lateUpdate)
                        : static_cast<bool>(definition.update) || static_cast<bool>(definition.onEvent))
                    return true;
            }
        }
        return false;
    }
    void RuntimeScripts::Frame::SetInput(const std::map<std::string, float>& input,
                                         const std::map<std::string, bool>& pressed)
    {
        Check(!pending_->prepared && !pending_->failed, "Cannot change prepared input");
        pending_->input = input;
        pending_->pressed = pressed;
    }
    bool RuntimeScripts::Frame::Prepare(std::string& error)
    {
        try
        {
            Check(!pending_->failed && !pending_->committed, "Cannot prepare failed script frame");
            const auto view = pending_->transaction.Read();
            for (const auto entity : view.WithRepeated<ScriptComponent>())
            {
                const auto scripts = view.RepeatedLease<ScriptComponent>(entity);
                for (const auto& script : *scripts)
                    if (script.enabled)
                    {
                        const auto found = pending_->owner.definitions->find(script.behaviour);
                        Check(found != pending_->owner.definitions->end(), "Unknown script behavior after phase");
                        CheckValue(script, found->second);
                    }
            }
            auto changes = SceneChangeSet{};
            const auto& previous = pending_->owner;
            if (previous.paused != pending_->paused)
                changes.fields.push_back({{}, {}, "runtime.paused"});
            if (previous.events != pending_->events)
                changes.fields.push_back({{}, {}, "runtime.events"});
            if (previous.effects.ui.values != pending_->effects.ui.values)
                changes.fields.push_back({{}, {}, "runtime.ui.values"});
            if (previous.effects.ui.texts != pending_->effects.ui.texts)
                changes.fields.push_back({{}, {}, "runtime.ui.texts"});
            if (previous.effects.scenes != pending_->effects.scenes)
                changes.fields.push_back({{}, {}, "runtime.sceneCommands"});
            if (previous.effects.audio != pending_->effects.audio)
                changes.fields.push_back({{}, {}, "runtime.audioCommands"});
            if (previous.effects.impulses != pending_->effects.impulses)
                changes.fields.push_back({{}, {}, "runtime.impulses"});
            if (previous.effects.animatorParameters != pending_->effects.animatorParameters)
                changes.fields.push_back({{}, {}, "runtime.animatorParameters"});
            if (previous.effects.clearAnimatorParameters != pending_->effects.clearAnimatorParameters)
                changes.fields.push_back({{}, {}, "runtime.animatorParameters"});
            if (previous.effects.ikTargets != pending_->effects.ikTargets)
                changes.fields.push_back({{}, {}, "runtime.ikTargets"});
            if (previous.effects.rootMotions != pending_->effects.rootMotions)
                changes.fields.push_back({{}, {}, "runtime.rootMotion"});
            for (const auto& [key, instance] : pending_->instances)
            {
                const auto old = previous.instances.find(key);
                if (old == previous.instances.end() || old->second->behavior != instance->behavior)
                    changes.fields.push_back({instance->objectId, instance->componentId, "runtime.instance"});
                if (old == previous.instances.end() || old->second->state != instance->state)
                    changes.fields.push_back({instance->objectId, instance->componentId, "runtime.state"});
                if (old == previous.instances.end() || old->second->dataState != instance->dataState)
                    changes.fields.push_back({instance->objectId, instance->componentId, "runtime.dataState"});
            }
            for (const auto& [key, instance] : previous.instances)
                if (!pending_->instances.contains(key))
                    changes.fields.push_back({instance->objectId, instance->componentId, "runtime.instance"});
            pending_->changes.Swap(changes);
            pending_->prepared = true;
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            pending_->failed = true;
            error = failure.what();
            return false;
        }
        catch (...)
        {
            pending_->failed = true;
            error = "Script preparation failed with an unknown exception";
            return false;
        }
    }
    bool RuntimeScripts::Frame::QueueEvent(ScriptEvent event, std::string& error)
    {
        try
        {
            Check(!pending_->prepared && !pending_->failed && !pending_->committed,
                  "Cannot queue an event to a prepared frame");
            RuntimeScriptScene::Impl sceneState{
                pending_->transaction, pending_->effects, pending_->events, pending_->queries, {}, {}, {}, 1, {}};
            RuntimeScriptScene scene(sceneState);
            scene.Emit(std::move(event));
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            pending_->failed = true;
            error = failure.what();
            return false;
        }
        catch (...)
        {
            pending_->failed = true;
            error = "Script event preparation failed";
            return false;
        }
    }
    bool RuntimeScripts::Frame::Commit() noexcept
    {
        if (!pending_->prepared || pending_->committed || pending_->failed)
            return false;
        auto& owner = pending_->owner;
        owner.instances.swap(pending_->instances);
        owner.events.swap(pending_->events);
        owner.effects.Swap(pending_->effects);
        owner.paused = pending_->paused;
        pending_->committed = true;
        return true;
    }
    void RuntimeScripts::Frame::FinishCallbacks() noexcept
    {
        if (!pending_->committed || pending_->finished)
            return;
        pending_->finished = true;
        for (const auto& stopped : pending_->stops)
            Finish(stopped);
    }
    const std::vector<ScriptEvent>& RuntimeScripts::Frame::PendingEvents() const noexcept
    {
        return pending_->events;
    }
    const RuntimeScriptEffects& RuntimeScripts::Frame::Effects() const noexcept
    {
        return pending_->effects;
    }
    const SceneChangeSet& RuntimeScripts::Frame::Changes() const noexcept
    {
        return pending_->changes;
    }
    RuntimeScripts::Frame RuntimeScripts::BeginFrame(WorldState::Transaction& transaction,
                                                     const std::map<std::string, float>& input,
                                                     const std::map<std::string, bool>& pressed,
                                                     RuntimeScriptQueries queries)
    {
        return Frame(*this, transaction, input, pressed, std::move(queries));
    }
    bool RuntimeScripts::Validate(WorldState::ReadView view, std::string& error) const
    {
        try
        {
            for (const auto entity : view.WithRepeated<ScriptComponent>())
            {
                const auto scripts = view.RepeatedLease<ScriptComponent>(entity);
                for (const auto& script : *scripts)
                    if (script.enabled)
                    {
                        const auto found = impl_->definitions->find(script.behaviour);
                        Check(found != impl_->definitions->end(), "Unknown script behavior");
                        CheckValue(script, found->second);
                    }
            }
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
    bool RuntimeScripts::QueueEvent(ScriptEvent event, std::string& error)
    {
        if (impl_->active || impl_->events.size() >= 4096 || event.name.empty() || event.name.size() > 128 ||
            event.sender.size() > 128 || event.target.size() > 128 || event.text.size() > 4096 ||
            !std::isfinite(event.value))
        {
            error = "Invalid event or active script frame";
            return false;
        }
        try
        {
            if (event.animation)
                AnimationEvents::ValidateOccurrence(*event.animation);
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
        impl_->events.push_back(std::move(event));
        error.clear();
        return true;
    }
    RuntimeScriptEffects RuntimeScripts::TakeEffects()
    {
        Check(!impl_->active, "Cannot drain pending effects");
        RuntimeScriptEffects result;
        result.Swap(impl_->effects);
        return result;
    }
    bool RuntimeScripts::DrainEffects(RuntimeScriptEffects& target) noexcept
    {
        if (impl_->active)
            return false;
        target.Clear();
        target.Swap(impl_->effects);
        return true;
    }
    bool RuntimeScripts::Paused() const noexcept
    {
        return impl_->paused;
    }
    bool RuntimeScripts::HasPhase(WorldState::ReadView view, ScriptPhase phase) const
    {
        for (const auto entity : view.WithRepeated<ScriptComponent>())
        {
            const auto scripts = view.RepeatedLease<ScriptComponent>(entity);
            for (const auto& script : *scripts)
            {
                if (!script.enabled)
                    continue;
                const auto found = impl_->definitions->find(script.behaviour);
                if (found == impl_->definitions->end())
                    continue;
                const auto& definition = found->second;
                if (phase == ScriptPhase::FixedUpdate ? static_cast<bool>(definition.fixedUpdate)
                    : phase == ScriptPhase::LateUpdate
                        ? static_cast<bool>(definition.lateUpdate)
                        : static_cast<bool>(definition.update) || static_cast<bool>(definition.onEvent))
                    return true;
            }
        }
        return false;
    }
    bool RuntimeScripts::Frame::ApplyEffects(const RuntimeScriptEffects& effects, std::string& error)
    {
        try
        {
            Check(!pending_->prepared && !pending_->committed && !pending_->failed,
                  "Cannot add prepared runtime effects");
            RuntimeScriptScene::Impl target{
                pending_->transaction, pending_->effects, pending_->events, pending_->queries, {}, {}, {}, 1, {}};
            RuntimeScriptScene scene(target);
            for (const auto& command : effects.scenes)
                if (command.unload)
                    scene.UnloadScene(command.scene);
                else
                    scene.LoadScene(command.scene, command.additive);
            for (const auto& [key, value] : effects.ui.values)
                scene.SetUiValue(key, value);
            for (const auto& [key, value] : effects.ui.texts)
                scene.SetUiText(key, value);
            for (const auto& command : effects.audio)
            {
                command.Validate();
                if (command.kind == AudioCommandKind::Play)
                    scene.PlayAudio(command.target);
                else
                    scene.AudioCue(command.target);
            }
            for (const auto& impulse : effects.impulses)
            {
                Check(scene.Read().Alive(impulse.body), "Impulse body generation expired");
                scene.AddImpulse(impulse.entity, impulse.value);
            }
            const auto resolve = [&](const auto& id)
            {
                const auto entity = scene.Find(id);
                Check(entity.has_value(), "Runtime effect owner missing");
                return *entity;
            };
            for (const auto& [id, names] : effects.clearAnimatorParameters)
                for (const auto& name : names)
                    scene.ClearAnimatorParameter(resolve(id), name);
            for (const auto& [id, values] : effects.animatorParameters)
                for (const auto& [name, value] : values)
                    scene.SetAnimatorParameter(resolve(id), name, value);
            for (const auto& [id, values] : effects.ikTargets)
                for (const auto& [name, value] : values)
                    scene.SetIkTarget(resolve(id), name, value);
            for (const auto& [id, value] : effects.rootMotions)
                scene.SetRootMotion(resolve(id), value);
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            pending_->failed = true;
            error = failure.what();
            return false;
        }
        catch (...)
        {
            pending_->failed = true;
            error = "Runtime effects failed with an unknown exception";
            return false;
        }
    }
    void RuntimeScripts::SetContent(std::shared_ptr<const Engine::ContentContext> content, std::filesystem::path root)
    {
        Check(!impl_->active, "Cannot replace active script content");
        impl_->content = std::move(content);
        impl_->root = std::move(root);
    }
    void RuntimeScripts::Stop(WorldState& world) noexcept
    {
        if (impl_->active)
            return;
        try
        {
            auto transaction = world.BeginTransaction();
            auto frame = BeginFrame(transaction);
            auto& pending = *frame.pending_;
            RuntimeScriptScene::Impl sceneState{
                transaction, pending.effects, pending.events, pending.queries, {}, {}, {}, 1, {}};
            RuntimeScriptScene scene(sceneState);
            std::vector<std::uint64_t> keys;
            for (const auto& [key, instance] : pending.instances)
            {
                static_cast<void>(instance);
                keys.push_back(key);
            }
            for (const auto key : keys)
                pending.Retire(key, scene);
            sceneState.Flush();
            std::string error;
            if (frame.Prepare(error) && transaction.Prepare(error) && transaction.Commit() && frame.Commit())
                frame.FinishCallbacks();
        }
        catch (...)
        {
        }
    }
}
