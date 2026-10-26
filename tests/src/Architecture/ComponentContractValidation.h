#pragma once
#include <SceneRuntime/Scene/SceneCollection.h>
#include <SceneRuntime/Scene/Prefab.h>
#include <LegacyScripts/ScriptRuntime.h>
#include <unordered_map>
#include "../../../SceneRuntime/src/Scene/SceneComponentJson.h"

namespace ComponentContractValidation
{
    /// <summary>全格納領域のコピーと、操作ごとの参照変換の契約を検証します。</summary>
    template <class Report> void Run(Report&& report)
    {
        using namespace SceneRuntime;
        ScenePlacement source;
        source.id = "original";
        source.name = "Original";
        source.parentId = "parent";
        source.position = {1, 2, 3};
        size_t count = 0;
        source.persistent = true;
        source.VisitComponentStorage(
            [&](auto& slot)
            {
                auto& component = [&]() -> auto&
                {
                    if constexpr (requires { slot.emplace(); })
                        return slot.emplace();
                    else
                        return slot.emplace_back();
                }();
                component.id = "component-" + std::to_string(count++);
            });
        ScenePlacement copy;
        copy.id = "copy";
        copy.name = "Copy";
        copy.CopyComponents(source);
        bool ids = copy.HasComponentId("transform") && !copy.HasComponentId("missing");
        source.VisitComponents(
            [&](const auto& component)
            {
                ids = ids && copy.HasComponentId(component.id);
            });
        report("component-storage-coverage", count == 36 && ids && copy.SameComponents(source) && copy.id == "copy" &&
                                                 copy.name == "Copy" && copy.parentId.empty() &&
                                                 copy.position == std::array<float, 3>{});
        copy.persistent = false;
        bool equality = !copy.SameComponents(source);
        copy.CopyComponents(source);
        copy.VisitComponentStorage(
            [&](auto& slot)
            {
                if constexpr (requires { slot.emplace(); })
                    slot->enabled = !slot->enabled;
                else
                    slot.front().enabled = !slot.front().enabled;
                equality = equality && !copy.SameComponents(source);
                copy.CopyComponents(source);
            });
        report("component-equality-coverage", equality && copy.SameComponents(source));

        source.SetModel("Assets/Models/Contract.obj");
        source.animator->states.front().clip = "Idle";
        const auto encodedComponents = WriteSceneComponents(source);
        ScenePlacement decoded;
        ReadSceneComponents(Engine::Json{{"components", encodedComponents}}, decoded, false);
        decoded.persistent = source.persistent;
        report("component-codec-coverage", encodedComponents.size() == 36 && decoded.SameComponents(source) &&
                                               encodedComponents.front().at("type") == "Joint" &&
                                               encodedComponents.back().at("type") == "Animation");

        ScenePlacement owner;
        owner.id = "owner";
        owner.parentId = "parent";
        owner.prefab = PrefabLink{"Assets/Prefabs/Source.prefab", "source", "root", ""};
        owner.joint.emplace().target = "owner";
        owner.navAgent.emplace().mesh = "owner";
        owner.ragdoll.emplace().bones.push_back({"bone", "owner"});
        owner.button.emplace().target = "owner";
        owner.button->sound = "outside";
        auto& menu = owner.canvas.emplace().menu.emplace();
        menu.entries.clear();
        menu.entries.push_back({0, "settings", "Assets/Scenes/Settings.json"});
        menu.entries.front().transitionCameraTarget = "owner";
        menu.configTransition.cameraTarget = "owner";
        menu.configTransition.returnCameraTarget = "outside";
        menu.returnScene = "Assets/Scenes/Return.json";
        ScriptComponent script;
        script.behaviour = "UnloadedReferenceContract";
        script.data["nested"] = {ScriptValue::Array{
            ScriptValue{ScriptObjectReference{"owner"}},
            ScriptValue{ScriptValue::Object{{"external", ScriptValue{ScriptObjectReference{"outside"}}}}}}};
        script.data["text"] = {std::string("owner")};
        owner.scripts.push_back(script);
        const auto before = owner;
        owner.RemapComponentReferences(
            std::unordered_map<std::string, std::string>{{"owner", "copy"}, {"root", "bad"}});
        size_t internal = 0, external = 0;
        owner.VisitObjectReferences(
            [&](std::string& id)
            {
                internal += id == "copy";
                external += id == "outside";
            });
        report("component-reference-coverage",
               internal == 7 && external == 3 && owner.id == "owner" && owner.parentId == "parent" &&
                   owner.prefab == before.prefab &&
                   std::get<std::string>(owner.scripts.front().data.at("text").value) == "owner" &&
                   owner.canvas->menu->returnScene == before.canvas->menu->returnScene &&
                   owner.canvas->menu->entries.front().target == "Assets/Scenes/Settings.json");
        bool actions = true;
        for (const auto* action : {"loadScene", "loadSceneAdditive", "unloadScene", "setState"})
        {
            owner.button->action = action;
            owner.button->target = "owner";
            owner.button->sound = "owner";
            owner.RemapComponentReferences(std::map<std::string, std::string>{{"owner", "changed"}});
            actions = actions && owner.button->target == "owner" && owner.button->sound == "changed";
        }
        report("component-reference-kinds", actions);
        SceneLayout existing, incoming;
        ScenePlacement collision;
        collision.id = "owner";
        existing.objects.push_back(collision);
        incoming.objects.push_back(before);
        incoming.objects.back().parentId.clear();
        incoming.objects.back().prefab.reset();
        incoming.objects.back().animator.emplace();
        SceneCollection collection;
        collection.Reset(existing);
        auto added = collection.Add(existing, incoming, "Incoming");
        auto& remapped = added.objects.back();
        internal = 0;
        remapped.VisitObjectReferences(
            [&](std::string& id)
            {
                internal += id == remapped.id;
            });
        report("component-additive-references", remapped.id != "owner" && internal == 7);

        SceneLayout hierarchy;
        ScenePlacement root;
        root.id = "template";
        ScriptComponent typed;
        typed.behaviour = "UnloadedReferenceContract";
        typed.data["child"] = {ScriptObjectReference{"child"}};
        typed.data["self"] = {ScriptObjectReference{"template"}};
        typed.data["outside"] = {ScriptObjectReference{"outside"}};
        root.scripts.push_back(typed);
        ScenePlacement child;
        child.id = "child";
        child.parentId = root.id;
        hierarchy.objects = {root, child};
        const auto linksMatch = [](const SceneLayout& layout, const std::string& rootId)
        {
            const auto parent = std::find_if(layout.objects.begin(), layout.objects.end(),
                                             [&](const auto& object)
                                             {
                                                 return object.id == rootId;
                                             });
            const auto descendant = std::find_if(layout.objects.begin(), layout.objects.end(),
                                                 [&](const auto& object)
                                                 {
                                                     return object.parentId == rootId;
                                                 });
            if (parent == layout.objects.end() || descendant == layout.objects.end())
                return false;
            const auto& data = parent->scripts.front().data;
            return std::get<ScriptObjectReference>(data.at("child").value).id == descendant->id &&
                   std::get<ScriptObjectReference>(data.at("self").value).id == rootId &&
                   std::get<ScriptObjectReference>(data.at("outside").value).id == "outside";
        };
        auto runtime = hierarchy;
        size_t next = 1;
        ScriptScene scene(runtime, next);
        const auto runtimeRoot = scene.Instantiate("template", {1, 2, 3});
        scene.Commit();
        report("component-runtime-references", linksMatch(runtime, runtimeRoot));
        const auto asset = Prefab::Extract(hierarchy, "template");
        SceneLayout placement;
        ScenePlacement outsideObject;
        outsideObject.id = "outside";
        placement.objects.push_back(outsideObject);
        const auto slot = Prefab::ExternalSlots(asset).begin()->first;
        const auto prefabRoot = Prefab::Instantiate(placement, asset, "Assets/Prefabs/Reference.prefab", {0, 0, 0},
                                                    {{slot, {outsideObject.id}}});
        report("component-prefab-references", linksMatch(placement, prefabRoot));
    }
}
