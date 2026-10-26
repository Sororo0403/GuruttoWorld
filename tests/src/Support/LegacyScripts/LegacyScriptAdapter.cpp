#include <LegacyScripts/LegacyScriptAdapter.h>
#include <LegacyScripts/ScriptRuntime.h>
#include <SceneRuntime/Scene/WorldStateReadAdapter.h>

namespace SceneRuntime
{
    void LegacyScriptAdapter::InvokeModern(const std::function<void(RuntimeScriptContext&)>& callback,
                                           ScriptContext& context)
    {
        auto source = context.scene ? context.scene->layout_ : SceneLayout{};
        const auto owner = std::find_if(source.objects.begin(), source.objects.end(),
                                        [&](const auto& value)
                                        {
                                            return value.id == context.object.id;
                                        });
        if (owner == source.objects.end())
            source.objects.push_back(context.object);
        else
            *owner = context.object;
        std::map<std::string, std::vector<ScriptComponent>> originalScripts;
        for (auto& object : source.objects)
        {
            originalScripts.emplace(object.id, object.scripts);
            object.scripts.clear();
            if (object.id == context.object.id)
            {
                ScriptComponent script;
                script.id = "test-native";
                script.behaviour = "TestNativeInvocation";
                script.parameters = context.parameters;
                if (context.data)
                    script.data = *context.data;
                object.scripts.push_back(std::move(script));
            }
        }
        auto definitions = std::make_shared<RuntimeScriptDefinitions>();
        auto& definition = (*definitions)["TestNativeInvocation"];
        for (const auto& [name, parameter] : context.parameters)
            definition.fields[name] = {parameter, -1e30f, 1e30f};
        if (context.data)
            definition.dataFields = *context.data;
        definition.update = [&](RuntimeScriptContext& modern)
        {
            modern.state = context.state;
            if (context.dataState)
                modern.dataState = *context.dataState;
            modern.event = context.event;
            modern.phase = context.phase;
            modern.seconds = context.seconds;
            callback(modern);
            context.state = modern.state;
            if (context.dataState)
                *context.dataState = modern.dataState;
        };
        WorldState world(WorldDefinition::Compile(source));
        auto transaction = world.BeginTransaction();
        RuntimeScripts scripts(definitions);
        scripts.SetContent(context.content, context.contentRoot);
        RuntimeScriptQueries queries;
        if (context.scene)
            queries.raycast = [&](const auto& origin, const auto& direction, float distance, unsigned int mask,
                                  bool triggers, const auto& ignore)
            {
                return context.scene->Raycast(origin, direction, distance, mask, triggers, ignore);
            };
        auto frame =
            scripts.BeginFrame(transaction, context.input ? *context.input : std::map<std::string, float>{},
                               context.pressed ? *context.pressed : std::map<std::string, bool>{}, std::move(queries));
        std::string error;
        if (!frame.Run(ScriptPhase::Update, context.seconds, error) || !frame.Prepare(error) ||
            !transaction.Prepare(error))
            throw std::runtime_error(error);
        const auto effects = frame.Effects();
        const auto events = frame.PendingEvents();
        if (!transaction.Commit() || !frame.Commit())
            std::terminate();
        frame.FinishCallbacks();
        auto result = world.Read().ExportLayout();
        for (auto& object : result.objects)
            if (const auto found = originalScripts.find(object.id); found != originalScripts.end())
                object.scripts = found->second;
        if (const auto own = std::find_if(result.objects.begin(), result.objects.end(),
                                          [&](const auto& value)
                                          {
                                              return value.id == context.object.id;
                                          });
            own != result.objects.end())
            context.object = *own;
        if (!context.scene)
            return;
        auto& scene = *context.scene;
        for (const auto& event : events)
            scene.Emit(event);
        for (const auto& before : source.objects)
        {
            const auto after = std::find_if(result.objects.begin(), result.objects.end(),
                                            [&](const auto& value)
                                            {
                                                return value.id == before.id;
                                            });
            if (after == result.objects.end())
                scene.Destroy(before.id);
            else if (before.id != context.object.id)
            {
                auto restored = before;
                restored.scripts = originalScripts.at(before.id);
                if (!restored.SameComponents(*after))
                    scene.SetComponents(before.id, *after);
            }
        }
        for (const auto& object : result.objects)
            if (!originalScripts.contains(object.id))
                scene.spawned_.push_back(object);
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
            if (command.kind == AudioCommandKind::Play)
                scene.PlayAudio(command.target);
            else
                scene.AudioCue(command.target);
        for (const auto& impulse : effects.impulses)
            if (const auto id = world.Read().ObjectId(impulse.entity))
                scene.AddImpulse(*id, impulse.value);
        for (const auto& [id, parameters] : effects.animatorParameters)
            for (const auto& [name, parameter] : parameters)
                scene.SetAnimatorParameter(id, name, parameter);
        for (const auto& [id, enabled] : effects.rootMotions)
            scene.SetRootMotion(id, enabled);
        for (const auto& [id, targets] : effects.ikTargets)
            for (const auto& [name, target] : targets)
                if (target)
                    scene.SetIkTarget(id, name, *target);
                else
                    scene.ClearIkTarget(id, name);
    }
    ScriptCallbacks LegacyScriptAdapter::Adapt(const RuntimeScriptDefinition& definition)
    {
        ScriptCallbacks result;
        for (const auto& pair : {std::make_pair(&result.start, &definition.start),
                                 {&result.update, &definition.update},
                                 {&result.fixedUpdate, &definition.fixedUpdate},
                                 {&result.lateUpdate, &definition.lateUpdate},
                                 {&result.onEvent, &definition.onEvent},
                                 {&result.prepareStop, &definition.prepareStop}})
            if (*pair.second)
                *pair.first = [callback = *pair.second](ScriptContext& context)
                {
                    InvokeModern(callback, context);
                };
        if (definition.pause)
            result.pause = [callback = definition.pause](ScriptContext& context, bool paused)
            {
                InvokeModern(
                    [&](RuntimeScriptContext& modern)
                    {
                        callback(modern, paused);
                    },
                    context);
            };
        if (definition.stop)
            result.stop = [callback = definition.stop](const ScriptStopContext& context)
            {
                RuntimeScriptStopContext modern;
                modern.object = context.object;
                modern.parameters = context.parameters;
                modern.state = context.state;
                if (context.data)
                    modern.data = *context.data;
                if (context.dataState)
                    modern.dataState = *context.dataState;
                modern.content = context.content;
                modern.contentRoot = context.contentRoot;
                callback(modern);
            };
        return result;
    }

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
    std::shared_ptr<const RuntimeScriptDefinitions> LegacyScriptAdapter::Capture(
        std::shared_ptr<const ScriptDefinitions> legacy)
    {
        if (!legacy)
            legacy = ScriptRegistry::Capture();
        auto result = std::make_shared<RuntimeScriptDefinitions>(*RuntimeScriptRegistry::Capture());
        for (const auto& [name, definition] : *legacy)
        {
            if (ScriptSchema::Builtins().contains(name))
                continue;
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
            result->insert_or_assign(name, std::move(next));
        }
        return result;
    }
}
