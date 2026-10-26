#include <SceneRuntime/Scripts/LegacyScriptAdapter.h>
#include <SceneRuntime/Scripts/ScriptRuntime.h>
#include <SceneRuntime/Scene/WorldStateReadAdapter.h>

namespace SceneRuntime
{
    void LegacyScriptAdapter::Invoke(const ScriptCallbacks& callbacks, Callback kind, RuntimeScriptContext& context,
                                     bool paused)
    {
        SceneLayout local;
        const auto owner = context.Read().Object(context.entity);
        if (!owner && !context.retiredOwner_)
            throw std::logic_error("Legacy callback owner is unavailable");
        local.objects.push_back(owner ? *owner : *context.retiredOwner_);
        const auto before = local.objects[0];
        auto& object = local.objects[0];
        size_t nextId = 1;
        ScriptScene scene(local, nextId);
        scene.runtime_ = &context.scene;
        scene.runtimeOwner_ = &object;
        scene.runtimeRead_.emplace(MakeWorldReadView(context.Read(), &object));
        ScriptContext legacy{object,         context.parameters, context.state, context.seconds,
                             context.input_, context.pressed_,   &scene,        context.event};
        legacy.data = &context.data;
        legacy.dataState = &context.dataState;
        legacy.phase = context.phase;
        legacy.content = context.content;
        legacy.contentRoot = context.contentRoot;
        if (kind == Callback::PrepareStop)
            legacy.scene = nullptr;
        switch (kind)
        {
        case Callback::Start:
            callbacks.start(legacy);
            break;
        case Callback::Update:
            callbacks.update(legacy);
            break;
        case Callback::Fixed:
            callbacks.fixedUpdate(legacy);
            break;
        case Callback::Late:
            callbacks.lateUpdate(legacy);
            break;
        case Callback::Event:
            callbacks.onEvent(legacy);
            break;
        case Callback::PrepareStop:
            callbacks.prepareStop(legacy);
            break;
        case Callback::Pause:
            callbacks.pause(legacy, paused);
            break;
        }
        if (object.id != before.id)
            throw std::runtime_error("Script cannot change a runtime entity ID");
        if (context.retiredOwner_)
        {
            *context.retiredOwner_ = std::move(object);
            return;
        }
        if (!context.SetLocalTransform({object.position, object.rotation, object.scale}).succeeded)
            throw std::runtime_error("Legacy script produced an invalid transform");
        context.scene.ApplyLegacyObject(context.entity, before, object);
        for (const auto& [id, components] : scene.components_)
        {
            const auto entity = context.scene.Find(id);
            if (!entity)
                throw std::runtime_error("Legacy component owner missing");
            context.scene.SetLegacyComponents(*entity, components);
        }
        for (const auto& [id, parent] : scene.parents_)
        {
            const auto entity = context.scene.Find(id);
            const auto target = parent.empty() ? std::optional<EntityHandle>{} : context.scene.Find(parent);
            if (!entity || (!parent.empty() && !target))
                throw std::runtime_error("Legacy parent owner missing");
            context.scene.SetParent(*entity, target.value_or(EntityHandle{}));
        }
        for (auto& event : scene.events)
            context.scene.Emit(std::move(event));
        for (auto& command : scene.sceneCommands)
            if (command.unload)
                context.scene.UnloadScene(std::move(command.scene));
            else
                context.scene.LoadScene(std::move(command.scene), command.additive);
        for (const auto& [key, value] : scene.uiCommands.values)
            context.scene.SetUiValue(key, value);
        for (auto& [key, value] : scene.uiCommands.texts)
            context.scene.SetUiText(key, std::move(value));
        for (const auto& command : scene.audioCommands)
        {
            command.Validate();
            if (command.kind == AudioCommandKind::Play)
                context.scene.PlayAudio(command.target);
            else
                context.scene.AudioCue(command.target);
        }
        for (const auto& [id, value] : scene.impulses)
        {
            const auto entity = context.scene.Find(id);
            if (entity)
                context.scene.AddImpulse(*entity, value);
        }
        for (const auto& [id, values] : scene.animatorParameters)
            if (const auto entity = context.scene.Find(id))
                for (const auto& [name, value] : values)
                    context.scene.SetAnimatorParameter(*entity, name, value);
        for (const auto& [id, values] : scene.ikTargets)
            if (const auto entity = context.scene.Find(id))
                for (const auto& [name, value] : values)
                    context.scene.SetIkTarget(*entity, name, value);
        for (const auto& [id, value] : scene.rootMotions)
            if (const auto entity = context.scene.Find(id))
                context.scene.SetRootMotion(*entity, value);
    }
    std::shared_ptr<const RuntimeScriptDefinitions> LegacyScriptAdapter::Capture()
    {
        const auto legacy = ScriptRegistry::Capture();
        auto result = std::make_shared<RuntimeScriptDefinitions>(*RuntimeScriptRegistry::Capture());
        for (const auto& [name, definition] : *legacy)
        {
            if (ScriptSchema::Builtins().contains(name))
                continue;
            if (result->contains(name))
                throw std::runtime_error("Duplicate script across SDK generations: " + name);
            RuntimeScriptDefinition next;
            static_cast<ScriptSchema&>(next) = definition;
            const auto wrap = [legacy, name](Callback kind)
            {
                return [legacy, name, kind](RuntimeScriptContext& context)
                {
                    Invoke(legacy->at(name), kind, context);
                };
            };
            if (definition.start)
                next.start = wrap(Callback::Start);
            if (definition.update)
                next.update = wrap(Callback::Update);
            if (definition.fixedUpdate)
                next.fixedUpdate = wrap(Callback::Fixed);
            if (definition.lateUpdate)
                next.lateUpdate = wrap(Callback::Late);
            if (definition.onEvent)
                next.onEvent = wrap(Callback::Event);
            if (definition.prepareStop)
                next.prepareStop = wrap(Callback::PrepareStop);
            if (definition.pause)
                next.pause = [legacy, name](RuntimeScriptContext& context, bool paused)
                {
                    Invoke(legacy->at(name), Callback::Pause, context, paused);
                };
            if (definition.stop)
                next.stop = [legacy, name](const RuntimeScriptStopContext& context)
                {
                    auto object = context.object;
                    auto state = context.state;
                    auto dataState = context.dataState;
                    ScriptContext value{object, context.parameters, state};
                    value.data = &context.data;
                    value.dataState = &dataState;
                    value.content = context.content;
                    value.contentRoot = context.contentRoot;
                    legacy->at(name).stop(ScriptStopContext(value));
                };
            result->emplace(name, std::move(next));
        }
        return result;
    }
}
