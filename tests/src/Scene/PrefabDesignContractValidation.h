#pragma once
#include <SceneRuntime/Scene/Prefab.h>
#include <SceneRuntime/Scripts/ScriptValueJson.h>
#include "../../../SceneRuntime/src/Scene/PrefabMerge.h"
#include "../../../Editor/src/Documents/SceneSnapshotCache.h"

namespace PrefabDesignContractValidation
{
    /// <summary>明示接続と保存・更新、および配列の安定IDと競合を検証します。</summary>
    template <class Report> void Run(const std::filesystem::path& root, Report&& report)
    {
        using namespace SceneRuntime;
        std::filesystem::create_directories(root / "Assets/Prefabs");
        const auto rejected = [](auto&& operation)
        {
            try
            {
                operation();
            }
            catch (const std::exception&)
            {
                return true;
            }
            return false;
        };
        ScenePlacement owner, external;
        owner.id = "owner";
        owner.scripts.emplace_back();
        owner.scripts.front().data["target"] = {ScriptObjectReference{"external"}};
        owner.scripts.front().data["self"] = {ScriptObjectReference{"owner"}};
        external.id = "external";
        SceneLayout original;
        original.objects = {owner, external};
        auto asset = Prefab::Extract(original, owner.id);
        const auto slot = Prefab::ExternalSlots(asset).begin()->first;
        const auto target = [](const SceneLayout& scene, const std::string& id)
        {
            const auto found = std::find_if(scene.objects.begin(), scene.objects.end(),
                                            [&](const auto& object)
                                            {
                                                return object.id == id;
                                            });
            return std::get<ScriptObjectReference>(found->scripts.front().data.at("target").value).id;
        };
        const auto path = std::filesystem::path("Assets/Prefabs/Scope.prefab");
        asset.Save(root / path);
        SceneLayout scene;
        scene.objects = {external};
        const auto unbound = Prefab::Instantiate(scene, asset, path, {});
        report("prefab-no-id-capture", target(scene, unbound).empty() && asset.prefabSlots.contains(slot));
        const auto connected = Prefab::Instantiate(scene, asset, path, {}, {{slot, {external.id}}});
        report("prefab-explicit-binding", target(scene, connected) == external.id);
        const auto before = scene.Serialize();
        report("prefab-invalid-binding-atomic", rejected(
                                                    [&]()
                                                    {
                                                        Prefab::BindReferences(scene, connected, {{slot, {"missing"}}});
                                                    }) &&
                                                    scene.Serialize() == before);
        Prefab::BindReferences(scene, unbound, {{slot, {external.id}}});
        auto loaded = SceneLayout::Parse(scene.Serialize());
        Prefab::Refresh(loaded, root);
        report("prefab-binding-roundtrip",
               target(loaded, unbound) == external.id && target(loaded, connected) == external.id);
        const auto once = loaded.Serialize();
        Prefab::Refresh(loaded, root);
        report("prefab-binding-refresh-idempotent", once == loaded.Serialize());
        Prefab::BindReferences(loaded, connected, {{slot, {""}}});
        report("prefab-unbind", target(loaded, connected).empty());
        scene.objects.back().name = "Applied name";
        Prefab::ApplyOverride(scene, connected, "/name", root);
        report("prefab-apply-property-scope", SceneLayout::Load(root / path).prefabSlots == asset.prefabSlots);
        asset.Save(root / path);
        const auto collisionPath = std::filesystem::path("Assets/Prefabs/CollisionScope.prefab");
        asset.Save(root / collisionPath);
        SceneLayout collisionScene;
        ScenePlacement collisionTarget;
        collisionTarget.id = owner.id;
        collisionScene.objects = {collisionTarget};
        const auto collisionRoot = Prefab::Instantiate(collisionScene, asset, collisionPath, {});
        collisionScene.objects.back().scripts.front().data["target"] = {ScriptObjectReference{collisionTarget.id}};
        Prefab::ApplyOverride(collisionScene, collisionRoot, "/components/script/data/value/target/value", root);
        const auto appliedCollision = SceneLayout::Load(root / collisionPath);
        report("prefab-apply-external-id-collision",
               target(appliedCollision, owner.id) != owner.id &&
                   appliedCollision.prefabSlots.contains(target(appliedCollision, owner.id)));
        auto boundOriginal = original;
        Prefab::Bind(boundOriginal, owner.id, path);
        Prefab::Refresh(boundOriginal, root);
        report("prefab-bind-existing", target(boundOriginal, owner.id) == external.id);
        auto legacy = Engine::Json::parse(boundOriginal.Serialize());
        legacy["version"] = 4;
        for (auto& object : legacy["objects"])
            if (object.contains("prefab"))
            {
                object["prefab"].erase("referenceVersion");
                object["prefab"].erase("externalBindings");
                auto prefabBaseline = Engine::Json::parse(object["prefab"]["baseline"].get<std::string>());
                auto& values = prefabBaseline["objects"][0]["components"][0]["data"]["value"];
                values["target"]["value"] = "external";
                object["prefab"]["baseline"] = prefabBaseline.dump();
            }
        auto old = SceneLayout::Parse(legacy.dump());
        const auto oldBefore = old.Serialize();
        report("prefab-legacy-binding-diagnostic", rejected(
                                                       [&]()
                                                       {
                                                           Prefab::Refresh(old, root);
                                                       }) &&
                                                       old.Serialize() == oldBefore);
        const auto oldSlot = Prefab::ExternalBindings(old, owner.id).begin()->first;
        Prefab::BindReferences(old, owner.id, {{oldSlot, {""}}});
        report("prefab-legacy-explicit-migration", target(old, owner.id).empty());
        // 安定IDの要素は、利用側の値変更と素材側の並べ替え・追加を独立に保持します。
        using Engine::Json;
        auto base =
            Json{{"type", "Animator"},
                 {"states", Json::array({Json{{"name", "A"}, {"speed", 1}}, Json{{"name", "B"}, {"speed", 2}}})}};
        auto local = base, remote = base;
        local["states"][0]["speed"] = 10;
        std::swap(remote["states"][0], remote["states"][1]);
        remote["states"].push_back(Json{{"name", "C"}, {"speed", 3}});
        const auto merged = PrefabMerge::Merge(base, local, remote);
        report("prefab-array-identity", merged["states"][0]["name"] == "B" && merged["states"][1]["speed"] == 10 &&
                                            merged["states"].size() == 3);
        remote = base;
        remote["states"].erase(0);
        report("prefab-array-delete-conflict", rejected(
                                                   [&]()
                                                   {
                                                       static_cast<void>(PrefabMerge::Merge(base, local, remote));
                                                   }));
        remote = base;
        remote["states"].push_back(remote["states"][0]);
        report("prefab-array-duplicate-conflict", rejected(
                                                      [&]()
                                                      {
                                                          static_cast<void>(PrefabMerge::Merge(base, local, remote));
                                                      }));
        auto atomic = Json{{"type", "Animator"}, {"transitions", Json::array({1, 2})}};
        auto atomicLocal = atomic, atomicRemote = atomic;
        atomicLocal["transitions"][0] = 10;
        atomicRemote["transitions"][1] = 20;
        report("prefab-array-atomic-diagnostic",
               rejected(
                   [&]()
                   {
                       static_cast<void>(PrefabMerge::Merge(atomic, atomicLocal, atomicRemote));
                   }));
        auto vector = Json{{"type", "Material"}, {"color", Json::array({1, 1, 1, 1})}};
        auto vectorLocal = vector, vectorRemote = vector;
        vectorLocal["color"][0] = 0;
        vectorRemote["color"][1] = 0;
        const auto color = PrefabMerge::Merge(vector, vectorLocal, vectorRemote);
        report("prefab-array-fixed-channels", color["color"][0] == 0 && color["color"][1] == 0);
        // 実際のスクリプト保存形式でも、idを持つ構造の配列は並べ替え後の同じ要素へ統合します。
        const auto identified = [](const std::string& id, float value)
        {
            return ScriptValue{ScriptValue::Object{{"id", {id}}, {"amount", {value}}}};
        };
        auto structured = asset;
        structured.objects.front().scripts.front().data["items"] = {
            ScriptValue::Array{identified("A", 1), identified("B", 2)}};
        const auto identityPath = std::filesystem::path("Assets/Prefabs/Identified.prefab");
        structured.Save(root / identityPath);
        SceneLayout identityScene;
        const auto identityRoot = Prefab::Instantiate(identityScene, structured, identityPath, {});
        auto& items =
            std::get<ScriptValue::Array>(identityScene.objects.front().scripts.front().data.at("items").value);
        std::get<ScriptValue::Object>(items.front().value).at("amount") = {10.0f};
        auto& sourceItems =
            std::get<ScriptValue::Array>(structured.objects.front().scripts.front().data.at("items").value);
        std::swap(sourceItems.front(), sourceItems.back());
        structured.Save(root / identityPath);
        Prefab::Refresh(identityScene, root);
        const auto& identityItems =
            std::get<ScriptValue::Array>(identityScene.objects.front().scripts.front().data.at("items").value);
        const auto& second = std::get<ScriptValue::Object>(identityItems.back().value);
        report("prefab-script-array-identity",
               second.at("id") == ScriptValue{std::string("A")} && second.at("amount") == ScriptValue{10.0f});
        const auto identityBefore = identityScene.Serialize();
        sourceItems.pop_back();
        structured.Save(root / identityPath);
        report("prefab-refresh-conflict-atomic", rejected(
                                                     [&]()
                                                     {
                                                         Prefab::Refresh(identityScene, root);
                                                     }) &&
                                                     identityScene.Serialize() == identityBefore);
        // 接続済み個体の再抽出で、同じ接続先の異なる口を潰さないことを確認します。
        auto twinAsset = asset;
        twinAsset.prefabSlots["second-slot"] = "second";
        twinAsset.objects.front().scripts.front().data["other"] = {ScriptObjectReference{"second-slot"}};
        SceneLayout twinScene;
        twinScene.objects = {external};
        const auto twinRoot = Prefab::Instantiate(twinScene, twinAsset, path, {},
                                                  {{slot, {external.id}}, {"second-slot", {external.id}}});
        const auto twinExtracted = Prefab::Extract(twinScene, twinRoot);
        const auto& twinData = twinExtracted.objects.front().scripts.front().data;
        report("prefab-distinct-external-slots",
               twinExtracted.prefabSlots.size() == 2 && twinData.at("target") != twinData.at("other"));
        for (const auto& actualId : {slot, std::string("prefab-scope-marker-1")})
        {
            SceneLayout overridden;
            ScenePlacement destination;
            destination.id = actualId;
            overridden.objects = {destination};
            const auto instance = Prefab::Instantiate(overridden, asset, path, {});
            overridden.objects.back().scripts.front().data["target"] = {ScriptObjectReference{actualId}};
            const auto exported = Prefab::Extract(overridden, instance);
            const auto exportedTarget = target(exported, owner.id);
            report(actualId == slot ? "prefab-slot-and-object-namespaces" : "prefab-private-marker-id-collision",
                   exportedTarget != slot && exported.prefabSlots.contains(exportedTarget) &&
                       exported.prefabSlots.at(exportedTarget) == actualId);
        }
        // 内側の接続先を外側Prefabの口へつなぎ、外側の配置時にだけシーンの個体へ解決します。
        SceneLayout outer;
        ScenePlacement outerRoot;
        outerRoot.id = "outer";
        outer.objects = {outerRoot, external};
        const auto nestedRoot = Prefab::Instantiate(outer, asset, path, {}, {{slot, {external.id}}});
        outer.objects.back().parentId = outerRoot.id;
        const auto nestedAsset = Prefab::Extract(outer, outerRoot.id);
        const auto nestedPath = std::filesystem::path("Assets/Prefabs/NestedScope.prefab");
        nestedAsset.Save(root / nestedPath);
        SceneLayout nestedScene;
        nestedScene.objects = {external};
        PrefabBindings nestedBindings;
        for (const auto& [key, label] : Prefab::ExternalSlots(nestedAsset))
        {
            static_cast<void>(label);
            nestedBindings[key] = {external.id};
        }
        const auto outerInstance = Prefab::Instantiate(nestedScene, nestedAsset, nestedPath, {}, nestedBindings);
        Prefab::Refresh(nestedScene, root);
        const auto nestedObject = std::find_if(nestedScene.objects.begin(), nestedScene.objects.end(),
                                               [&](const auto& object)
                                               {
                                                   return object.parentId == outerInstance;
                                               });
        report("prefab-nested-external-binding",
               nestedObject != nestedScene.objects.end() && target(nestedScene, nestedObject->id) == external.id);
        const auto nestedOnce = nestedScene.Serialize();
        Prefab::Refresh(nestedScene, root);
        report("prefab-nested-binding-idempotent", nestedOnce == nestedScene.Serialize());
        auto variant = Prefab::Variant(scene, connected);
        const auto variantPath = std::filesystem::path("Assets/Prefabs/ScopeVariant.prefab");
        variant.Save(root / variantPath);
        Prefab::Refresh(variant, root);
        SceneLayout variantScene;
        variantScene.objects = {external};
        PrefabBindings variantBindings;
        for (const auto& [key, label] : Prefab::ExternalSlots(variant))
        {
            static_cast<void>(label);
            variantBindings[key] = {external.id};
        }
        const auto variantRoot = Prefab::Instantiate(variantScene, variant, variantPath, {}, variantBindings);
        Prefab::Refresh(variantScene, root);
        report("prefab-variant-external-binding", target(variantScene, variantRoot) == external.id);
        const auto variantOnce = variantScene.Serialize();
        Prefab::Refresh(variantScene, root);
        report("prefab-variant-binding-idempotent", variantOnce == variantScene.Serialize());
        auto grid = Json{{"type", "Tilemap"}, {"columns", 2}, {"rows", 2}, {"tiles", Json::array({0, 0, 0, 0})}};
        auto gridLocal = grid, gridRemote = grid;
        gridLocal["tiles"][0] = 1;
        gridRemote["tiles"][1] = 2;
        const auto cells = PrefabMerge::Merge(grid, gridLocal, gridRemote);
        report("prefab-grid-cell-merge", cells["tiles"][0] == 1 && cells["tiles"][1] == 2);
        gridRemote["columns"] = 4;
        gridRemote["rows"] = 1;
        report("prefab-grid-resize-conflict",
               rejected(
                   [&]()
                   {
                       static_cast<void>(PrefabMerge::Merge(grid, gridLocal, gridRemote));
                   }));
        const ScriptValue::Object recordA{{"id", {ScriptObjectReference{"A"}}}, {"amount", {1.0f}}};
        const ScriptValue::Object recordB{{"id", {ScriptObjectReference{"B"}}}, {"amount", {2.0f}}};
        const ScriptValue referenceRecords{ScriptValue::Array{ScriptValue{recordA}, ScriptValue{recordB}}};
        const auto referenceArray = WriteScriptValue(referenceRecords);
        Json referenceBase{{"type", "Script"}};
        referenceBase["data"] = Json{{"kind", "object"}, {"value", Json::object()}};
        referenceBase["data"]["value"]["refs"] = referenceArray;
        auto referenceLocal = referenceBase, referenceRemote = referenceBase;
        referenceLocal["data"]["value"]["refs"]["value"][0]["value"]["amount"]["value"] = 10;
        referenceRemote["data"]["value"]["refs"]["value"][1]["value"]["amount"]["value"] = 20;
        const auto referenceMerged = PrefabMerge::Merge(referenceBase, referenceLocal, referenceRemote);
        report("prefab-script-reference-id-is-data",
               referenceMerged["data"]["value"]["refs"]["value"][0]["value"]["amount"]["value"] == 10 &&
                   referenceMerged["data"]["value"]["refs"]["value"][1]["value"]["amount"]["value"] == 20);
        auto namedFields = asset;
        namedFields.objects.front().scripts.front().data["kind"] = {std::string("old kind")};
        namedFields.objects.front().scripts.front().data["type"] = {std::string("old type")};
        namedFields.objects.front().scripts.front().data["amount"] = {1.0f};
        const auto namedPath = std::filesystem::path("Assets/Prefabs/NamedFields.prefab");
        namedFields.Save(root / namedPath);
        SceneLayout namedScene;
        Prefab::Instantiate(namedScene, namedFields, namedPath, {});
        namedScene.objects.front().scripts.front().data["amount"] = {10.0f};
        namedFields.objects.front().scripts.front().data["kind"] = {std::string("new kind")};
        namedFields.objects.front().scripts.front().data["type"] = {std::string("new type")};
        namedFields.Save(root / namedPath);
        Prefab::Refresh(namedScene, root);
        const auto& namedData = namedScene.objects.front().scripts.front().data;
        report("prefab-user-kind-and-type-fields", namedData.at("kind") == ScriptValue{std::string("new kind")} &&
                                                       namedData.at("type") == ScriptValue{std::string("new type")} &&
                                                       namedData.at("amount") == ScriptValue{10.0f});
        auto transformAsset = asset;
        ScenePlacement child;
        child.id = "child";
        child.parentId = owner.id;
        transformAsset.objects.push_back(child);
        const auto transformPath = std::filesystem::path("Assets/Prefabs/Transforms.prefab");
        transformAsset.Save(root / transformPath);
        SceneLayout transforms;
        Prefab::Instantiate(transforms, transformAsset, transformPath, {5, 0, 0});
        transforms.objects.back().position[0] = 10;
        transforms.objects.back().rotation[0] = 10;
        transforms.objects.back().scale[0] = 2;
        transformAsset.objects.back().position[1] = 20;
        transformAsset.objects.back().rotation[1] = 20;
        transformAsset.objects.back().scale[1] = 3;
        transformAsset.objects.front().position[0] = 100;
        transformAsset.Save(root / transformPath);
        Prefab::Refresh(transforms, root);
        report("prefab-transform-channels", transforms.objects.back().position == std::array<float, 3>{10, 20, 0} &&
                                                transforms.objects.back().rotation == std::array<float, 3>{10, 20, 0} &&
                                                transforms.objects.back().scale == std::array<float, 3>{2, 3, 1} &&
                                                transforms.objects.front().position[0] == 5);
        const auto ikBase = Json{{"type", "Animator"},
                                 {"ik", Json::array({Json{{"name", "Reach"}, {"target", Json::array({0, 0, 0})}}})}};
        auto ikLocal = ikBase, ikRemote = ikBase;
        ikLocal["ik"][0]["target"][0] = 10;
        ikRemote["ik"][0]["target"][1] = 20;
        const auto ikMerged = PrefabMerge::Merge(ikBase, ikLocal, ikRemote);
        report("prefab-nested-vector-channels",
               ikMerged["ik"][0]["target"][0] == 10 && ikMerged["ik"][0]["target"][1] == 20);
        static_cast<void>(identityRoot);
        static_cast<void>(nestedRoot);
        Editor::SceneSnapshotCache cache;
        const auto snapshot = cache.Get(scene);
        scene.prefabSlots["unused-slot"] = "New external slot";
        report("prefab-slot-snapshot", cache.Get(scene) != snapshot);
    }
}
