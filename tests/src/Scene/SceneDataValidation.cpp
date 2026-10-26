#include "../Support/TestGameContent.h"
#include <SceneRuntime/Scene/Prefab.h>
#include <SceneRuntime/Scripts/ScriptSchema.h>
#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <SceneRuntime/Ui/UiState.h>
#include <Engine/Assets/AssetPath.h>
#include <Engine/Core/Utf8.h>
#include <fstream>
#include <iostream>
#include <chrono>
#include <limits>

namespace Engine
{
    class AssetDatabase;
}
namespace SceneRuntime
{
    class SceneWorld;
    class PhysicsWorld;
}
template <class T>
concept Complete = requires { sizeof(T); };
static_assert(!Complete<Engine::AssetDatabase>);
static_assert(!Complete<SceneRuntime::SceneWorld>);
static_assert(!Complete<SceneRuntime::PhysicsWorld>);
#if defined(__d3d12_h__) || defined(_WINDOWS_)
#error SceneData must not include platform or rendering APIs
#endif

namespace
{
    using namespace SceneRuntime;
    void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    template <class Operation> void Reject(Operation operation, const char* message)
    {
        bool rejected = false;
        try
        {
            operation();
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected, message);
    }
    SceneLayout Asset(const std::string& id)
    {
        SceneLayout result;
        ScenePlacement object;
        object.id = id;
        result.objects.push_back(object);
        return result;
    }
    void ValidateUtf8()
    {
        for (const std::string text :
             {std::string{}, std::string("ASCII"), std::string("日本語"), std::string("\xC2\x80"),
              std::string("\xDF\xBF"), std::string("\xE0\xA0\x80"), std::string("\xEF\xBF\xBF"),
              std::string("\xF0\x90\x80\x80"), std::string("\xF4\x8F\xBF\xBF"), std::string("a\0b", 3)})
            Require(Engine::ValidUtf8(text), "valid UTF-8 scalar boundary");
        for (const std::string text : {"\x80", "\xC0\x80", "\xC1\xBF", "\xC2", "\xE0\x80\x80", "\xED\xA0\x80",
                                       "\xF0\x80\x80\x80", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xFF", "\xE2x"})
            Require(!Engine::ValidUtf8(text), "invalid UTF-8 scalar boundary");
        auto invalid = Asset("invalid-utf8");
        invalid.objects.front().name = "\xFF";
        Reject(
            [&]
            {
                static_cast<void>(invalid.Serialize());
            },
            "saved representation rejects invalid UTF-8");
        Reject(
            [&]
            {
                static_cast<void>(invalid.Normalized());
            },
            "direct normalization rejects invalid UTF-8");
        invalid.objects.front().name.clear();
        invalid.objects.front().canvas.emplace();
        invalid.objects.front().rectTransform.emplace();
        invalid.Validate();
        invalid.objects.front().canvas->stateDefaults["\xFF"] = 0;
        Reject(
            [&]
            {
                static_cast<void>(invalid.Normalized());
            },
            "direct normalization rejects invalid UTF-8 keys");
    }
    void ValidateRoundtrip()
    {
        size_t files = 0;
        for (const auto& folder : {"Tools/GameProject/Templates/MinimalGame/Assets/Scenes",
                                   WP1_TEST_GAME_ROOT "/Assets/Scenes", WP1_TEST_GAME_ROOT "/Assets/Prefabs"})
        {
            if (!std::filesystem::exists(folder))
                continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folder))
            {
                const auto extension = std::string_view(folder).ends_with("Scenes") ? ".json" : ".prefab";
                if (entry.path().extension() != extension)
                    continue;
                std::ifstream input(entry.path(), std::ios::binary);
                const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                const auto parsed = SceneLayout::Parse(source);
                const auto canonical = parsed.Serialize();
                Require(parsed.Normalized().Serialize() == canonical,
                        "direct normalization matches saved representation");
                Require(SceneLayout::Parse(canonical).Serialize() == canonical, "scene/prefab canonical roundtrip");
                ++files;
            }
        }
        Require(files > 0, "real scene fixtures must be present");
        const auto& builtins = ScriptSchema::Builtins();
        Require(builtins.size() == 6 && builtins.at("Spin").fields.at("speed").initial == 90 &&
                    builtins.at("FollowTarget").dataFields.contains("target"),
                "builtin declarations are available without runtime callbacks or registry");
        const auto validated = Asset("nonfinite");
        validated.Validate();
        auto nonfinite = validated;
        nonfinite.objects.front().material.emplace().shaderParameters[0] = std::numeric_limits<float>::infinity();
        Reject(
            [&]
            {
                static_cast<void>(nonfinite.Serialize());
            },
            "saved representation rejects nonfinite components");
        Reject(
            [&]
            {
                static_cast<void>(nonfinite.Normalized());
            },
            "direct normalization rejects nonfinite components");
        nonfinite.objects.front().material->shaderParameters[0] = std::numeric_limits<float>::quiet_NaN();
        Reject(
            [&]
            {
                nonfinite.Validate();
            },
            "direct validation rejects NaN component values");
        UiState state;
        Require(state.Assign("mode=1&intro=0.5") && state.Matches("mode=1"), "headless UI expressions");
        const auto before = state.values;
        Require(!state.Assign("mode=2&bad") && state.values == before, "UI expression rejection is atomic");
        Require(!Engine::AssetPath::Valid("Assets/../escape") && Engine::AssetPath::Valid("Assets/Models/a.obj"),
                "shared logical path validation");
        std::cout << "PASS: " << files << " real scene/prefab roundtrips without Engine or SceneRuntime linkage\n";
    }
    void ValidateResolution()
    {
        const std::filesystem::path leafPath = "Assets/Prefabs/Leaf.prefab";
        const std::filesystem::path parentPath = "Assets/Prefabs/Parent.prefab";
        auto leaf = Asset("leaf");
        auto parent = Asset("parent");
        Prefab::Instantiate(parent, leaf, leafPath, {});
        parent.objects.back().parentId = "parent";
        SceneLayout scene;
        for (size_t i = 0; i < 16; ++i)
            Prefab::Instantiate(scene, parent, parentPath, {static_cast<float>(i), 0, 0});
        const auto overridden = scene.objects.front().id;
        scene.objects.front().name = "local override";
        leaf.objects.front().name = "new leaf";
        parent.objects.front().name = "new parent";
        std::map<std::filesystem::path, size_t> reads;
        const auto loader = [&](const std::filesystem::path& path)
        {
            ++reads[path];
            if (path == leafPath)
                return leaf;
            if (path == parentPath)
                return parent;
            throw std::runtime_error("missing source");
        };
        Prefab::RefreshPrepared(scene, loader);
        Require(reads[leafPath] == 1 && reads[parentPath] == 1, "each dependency is read once per resolution");
        Require(scene.objects.front().name == "local override" && scene.objects[1].name == "new leaf" &&
                    scene.objects[2].name == "new parent",
                "nested sources merge while preserving local overrides");
        Prefab::RevertInstance(scene, overridden);
        Require(scene.objects.front().name == "new parent", "headless revert uses resolved baseline");
        const auto before = scene.Serialize();
        Reject(
            [&]
            {
                Prefab::RefreshPrepared(scene,
                                        [](const auto&) -> SceneLayout
                                        {
                                            throw std::runtime_error("read");
                                        });
            },
            "reader failure must reject resolution");
        Require(scene.Serialize() == before, "reader failure preserves the whole destination");
        auto cyclic = parent;
        cyclic.objects[1].prefab->asset = parentPath;
        Reject(
            [&]
            {
                Prefab::RefreshPrepared(scene,
                                        [&](const auto&)
                                        {
                                            return cyclic;
                                        });
            },
            "dependency cycle rejection");
        Require(scene.Serialize() == before, "cycle failure preserves the whole destination");
        auto invalid = leaf;
        invalid.objects.front().scale[0] = 0;
        Reject(
            [&]
            {
                Prefab::RefreshPrepared(scene,
                                        [&](const auto& path)
                                        {
                                            return path == leafPath ? invalid : parent;
                                        });
            },
            "invalid nested source rejection");
        Require(scene.Serialize() == before, "invalid nested source preserves the whole destination");
        auto partial = scene;
        const auto missingRoot = partial.objects.back().prefab->rootId;
        const std::filesystem::path missingPath = "Assets/Prefabs/ZMissing.prefab";
        for (auto& object : partial.objects)
            if (object.prefab && object.prefab->rootId == missingRoot)
                object.prefab->asset = missingPath;
        const auto partialBefore = partial.Serialize();
        size_t loaded = 0;
        Reject(
            [&]
            {
                Prefab::RefreshPrepared(partial,
                                        [&](const auto& path)
                                        {
                                            if (path == missingPath)
                                                throw std::runtime_error("late source failure");
                                            ++loaded;
                                            auto source = loader(path);
                                            if (path == parentPath)
                                                source.objects.front().name = "uncommitted change";
                                            return source;
                                        });
            },
            "failure after earlier instance merge must reject resolution");
        Require(loaded == 2 && partial.Serialize() == partialBefore,
                "late source failure rolls back already merged instances");
        parent.objects.front().name = "new parent after retry";
        reads.clear();
        Prefab::RefreshPrepared(scene, loader);
        Require(reads[leafPath] == 1 && reads[parentPath] == 1 &&
                    scene.objects.front().name == "new parent after retry",
                "failure does not poison the resolver and later source changes are observed");
        std::cout << "PASS: nested Prefab reuse, overrides, revert, cycles, read/validation failure and retry\n";
    }
}

int main()
{
    try
    {
        ValidateUtf8();
        ValidateRoundtrip();
        ValidateResolution();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
