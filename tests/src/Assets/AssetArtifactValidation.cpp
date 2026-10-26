#include <Engine/Assets/AssetArtifact.h>
#include <Engine/Assets/ModelImportInputs.h>
#include <Engine/Animation/SkeletonAssets.h>
#include <Engine/Graphics/Models/ModelAssets.h>
#include <Engine/Assets/TextureAssets.h>
#include <Engine/Core/Json.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

namespace
{
    using namespace Engine;
    size_t contracts = 0;
    void Require(bool value, const char* name)
    {
        ++contracts;
        if (!value)
            throw std::runtime_error(name);
        std::cout << "PASS: " << name << std::endl;
    }
    void Wait(const ArtifactTicket& ticket)
    {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!ticket.Ready() && std::chrono::steady_clock::now() < limit)
            std::this_thread::yield();
        if (!ticket.Ready())
            throw std::runtime_error("Artifact request timed out");
    }
    void Write(const std::filesystem::path& path, std::string_view bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << bytes;
        if (!output)
            throw std::runtime_error("Cannot write fixture");
    }
    ArtifactDescriptor Descriptor()
    {
        return {"mesh", "asset:stable-model", "source", "assimp:fixed", "scale=1", "windows-x64", "toolset:145", {}};
    }
    void ModelInputs(const std::filesystem::path& root)
    {
        const auto model = root / "Fixture.gltf";
        const auto buffer = root / "外部 buffer.bin";
        Write(buffer, "first");
        Write(model, R"({"asset":{"version":"2.0"},"buffers":[{"uri":"%E5%A4%96%E9%83%A8%20buffer.bin"}]})");
        auto inputs = ModelImportInputs::Capture(model, "test-mesh");
        inputs.sources.Describe(inputs.descriptor);
        Require(inputs.sources.Unchanged() && inputs.descriptor.dependencies.size() == 3,
                "model-artifact-captures-escaped-buffer-and-optional-metadata");
        const auto stamp = std::filesystem::last_write_time(buffer);
        Write(buffer, "other");
        std::filesystem::last_write_time(buffer, stamp);
        Require(!inputs.sources.Unchanged(), "model-artifact-invalidates-on-external-buffer-content");
        auto next = ModelImportInputs::Capture(model, "test-mesh");
        next.sources.Describe(next.descriptor);
        Require(inputs.descriptor.Key() != next.descriptor.Key(), "model-artifact-key-includes-external-buffer");
        Write(root / "Optional.obj", "mtllib absent.mtl\nv 0 0 0\n");
        const auto missing = ModelImportInputs::Capture(root / "Optional.obj", "test-mesh");
        Write(root / "absent.mtl", "newmtl material\n");
        Require(!missing.sources.Unchanged(), "model-artifact-invalidates-on-new-optional-mtl");
        Write(root / "fallback.obj", "mtllib unavailable.mtl\nv 0 0 0\n");
        Write(root / "fallback.mtl", "newmtl material\nmap_Kd -o 0 0 0 texture with spaces.png\n");
        Write(root / "texture with spaces.png", "first");
        const auto fallback = ModelImportInputs::Capture(root / "fallback.obj", "test-mesh");
        Write(root / "texture with spaces.png", "other");
        Require(!fallback.sources.Unchanged(), "model-artifact-captures-assimp-fallback-mtl-and-texture-with-spaces");
        auto textureBefore = fallback.descriptor;
        fallback.sources.Describe(textureBefore);
        auto textureAfter = ModelImportInputs::Capture(root / "fallback.obj", "test-mesh");
        textureAfter.sources.Describe(textureAfter.descriptor);
        Require(textureBefore.Key() == textureAfter.descriptor.Key(),
                "model-cpu-key-excludes-texture-bytes-while-proof-still-detects-changes");
        const auto triangle = root / "Triangle.obj";
        Write(triangle, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        std::string modelError;
        const auto cpu = ModelAssets::Load(triangle, modelError);
        if (!cpu)
            throw std::runtime_error(modelError);
        Require(cpu == ModelAssets::Load(triangle, modelError) && cpu->size() == 1 && cpu->front().indices.size() == 3,
                "model-cpu-artifact-roundtrip-and-shared-lease");
        const auto cooked = root / "Cooked/static-mesh";
        Require(ModelAssets::Cook(triangle, cooked, modelError), "model-cpu-artifact-cook-shares-runtime-format");
        auto modelInputs = ModelImportInputs::Capture(triangle, "static-mesh");
        modelInputs.sources.Describe(modelInputs.descriptor);
        Require(ArtifactStore({}, cooked).Read(modelInputs.descriptor.Key()) &&
                    Json::parse(std::ifstream(cooked / (modelInputs.descriptor.Key() + ".recipe.json"))).at("key") ==
                        modelInputs.descriptor.Key(),
                "model-cpu-cook-includes-input-recipe-and-read-only-fallback");
        Write(std::filesystem::path(triangle.wstring() + L".meta"),
              Json{{"version", 1}, {"id", "1123456789abcdef0123456789abcdef"}, {"scale", 2}, {"flipV", false}}.dump());
        const auto scaled = ModelAssets::Load(triangle, modelError);
        if (!scaled)
            throw std::runtime_error(modelError);
        float maximum = 0;
        for (const auto& vertex : scaled->front().vertices)
            maximum = std::max(maximum, vertex.position[0]);
        Require(scaled != cpu && maximum == 2, "model-cpu-reimport-respects-new-settings-with-old-lease-valid");
        const auto image = root / "Image.png";
        std::filesystem::copy_file("tests/fixtures/Player/Assets/Textures/Checker.png", image);
        std::string imageError;
        const auto pixels = TextureAssets::Load(image, imageError);
        if (!pixels)
            throw std::runtime_error(imageError);
        Require(pixels == TextureAssets::Load(image, imageError) && !pixels->levels.empty() && !pixels->compressed,
                "texture-cpu-decode-roundtrip-and-shared-lease-without-renderer");
        Require(TextureAssets::Cook(image, root / "Cooked/texture-mips", imageError),
                "texture-cpu-cook-uses-runtime-format-and-input-recipe");
        Write(std::filesystem::path(image.wstring() + L".meta"),
              Json{{"version", 1},
                   {"id", "2123456789abcdef0123456789abcdef"},
                   {"scale", 1},
                   {"flipV", false},
                   {"texture", {{"mipmaps", true}, {"srgb", true}, {"maxSize", 16384}, {"compression", "none"}}}}
                  .dump());
        const auto mips = TextureAssets::Load(image, imageError);
        if (!mips)
            throw std::runtime_error(imageError);
        Require(mips != pixels && mips->levels.size() > 1 && pixels->levels.size() == 1,
                "texture-cpu-reimport-keeps-old-mips-and-observes-settings");
        const auto fixture = root / "Animated.gltf";
        std::filesystem::copy_file(std::filesystem::path(WP1_TEST_GAME_ROOT) / "Assets/Models/AnimatedBox.gltf",
                                   fixture);
        auto metadata = Json{{"version", 1},
                             {"id", "0123456789abcdef0123456789abcdef"},
                             {"scale", 1},
                             {"flipV", false},
                             {"previousPaths", Json::array()}};
        Write(std::filesystem::path(fixture.wstring() + L".meta"), metadata.dump());
        const auto first = SkeletonAssets::Request(fixture);
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!first.Ready() && std::chrono::steady_clock::now() < limit)
            std::this_thread::yield();
        std::string error;
        const auto old = first.Publish(error);
        if (!old)
            throw std::runtime_error(error);
        const auto same = SkeletonAssets::Load(fixture, error);
        Require(same == old && !old->meshes.empty() && !old->clips.empty(),
                "skeleton-artifact-roundtrip-and-shared-immutable-lease");
        auto json = Json::parse(std::ifstream(fixture));
        json["nodes"][0]["name"] = "new-generation";
        Write(fixture, json.dump());
        Require(!first.Publish(error), "skeleton-artifact-rejects-result-after-source-change");
        const auto fresh = SkeletonAssets::Load(fixture, error);
        if (!fresh)
            throw std::runtime_error(error);
        Require(fresh != old && old->nodes[0].name != fresh->nodes[0].name,
                "skeleton-artifact-reimport-keeps-previous-session-lease");
        auto beforeRename = ModelImportInputs::Capture(fixture, "skeletal-mesh");
        beforeRename.sources.Describe(beforeRename.descriptor);
        const auto renamed = root / "Renamed.gltf";
        std::filesystem::rename(fixture, renamed);
        std::filesystem::rename(std::filesystem::path(fixture.wstring() + L".meta"),
                                std::filesystem::path(renamed.wstring() + L".meta"));
        metadata["previousPaths"] = Json::array({"Assets/Models/Animated.gltf"});
        Write(std::filesystem::path(renamed.wstring() + L".meta"), metadata.dump());
        auto afterRename = ModelImportInputs::Capture(renamed, "skeletal-mesh");
        afterRename.sources.Describe(afterRename.descriptor);
        const auto moved = SkeletonAssets::Load(renamed, error);
        Require(moved && beforeRename.descriptor.Key() == afterRename.descriptor.Key() &&
                    SkeletonAssets::SameArtifact(fresh, moved) && !beforeRename.sources.Unchanged(),
                "skeleton-artifact-rename-preserves-semantic-key-and-rejects-old-source-proof");
    }
    void Validate(const std::filesystem::path& root)
    {
        Require(ArtifactHash::Bytes("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" &&
                    ArtifactHash::Bytes("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "artifact-sha256-known-vectors");
        auto descriptor = Descriptor();
        descriptor.dependencies = {{"texture:b", "2"}, {"texture:a", "1"}};
        auto reordered = descriptor;
        std::reverse(reordered.dependencies.begin(), reordered.dependencies.end());
        Require(descriptor.Key() == reordered.Key(), "artifact-key-orders-dependencies");
        auto changed = descriptor;
        changed.dependencies.front().revision = "3";
        Require(descriptor.Key() != changed.Key(), "artifact-key-includes-dependency-contents");
        changed = descriptor;
        changed.settings += ":changed";
        Require(descriptor.Key() != changed.Key(), "artifact-key-includes-import-settings");
        changed = descriptor;
        changed.importerVersion += ":changed";
        Require(descriptor.Key() != changed.Key(), "artifact-key-includes-importer-version");
        changed = descriptor;
        changed.platform += ":changed";
        Require(descriptor.Key() != changed.Key(), "artifact-key-includes-platform");
        changed = descriptor;
        changed.compiler += ":changed";
        Require(descriptor.Key() != changed.Key(), "artifact-key-includes-compiler");
        changed = descriptor;
        changed.assetId = "a:bc";
        changed.sourceRevision = "d";
        auto boundary = changed;
        boundary.assetId = "a";
        boundary.sourceRevision = "bc:d";
        Require(changed.Key() != boundary.Key(), "artifact-key-does-not-alias-field-boundaries");
        bool duplicateRejected = false;
        try
        {
            changed.dependencies.push_back(changed.dependencies.front());
            static_cast<void>(changed.Key());
        }
        catch (const std::invalid_argument&)
        {
            duplicateRejected = true;
        }
        Require(duplicateRejected, "artifact-rejects-duplicate-dependency-identities");

        {
            ArtifactRequests transient({}, false);
            std::weak_ptr<const ArtifactData> released;
            {
                const auto ticket = transient.Request(Descriptor(), {},
                                                      []
                                                      {
                                                          return "transient";
                                                      });
                Wait(ticket);
                const auto data = ticket.Poll();
                released = data;
                Require(transient.Publish(ticket), "artifact-transient-owner-accepts-current-result");
            }
            const auto releaseLimit = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            while (!released.expired() && std::chrono::steady_clock::now() < releaseLimit)
                std::this_thread::yield();
            Require(released.expired(), "artifact-transient-import-owner-does-not-retain-all-cpu-blobs");
        }
        const auto source = root / "source.bin";
        const auto optional = root / "optional.bin";
        Write(source, "first");
        const auto sources = ArtifactSources::Capture({{"model", source}, {"optional", optional}});
        Require(sources.Unchanged(), "artifact-captures-existing-and-missing-inputs");
        Write(optional, "added");
        Require(!sources.Unchanged(), "artifact-detects-new-optional-dependency");
        const auto stamp = std::filesystem::last_write_time(source);
        const auto existing = ArtifactSources::Capture({{"model", source}});
        Write(source, "other");
        std::filesystem::last_write_time(source, stamp);
        Require(!existing.Unchanged(), "artifact-detects-same-size-same-time-source-change");
        const auto renamed = root / "renamed.bin";
        Write(renamed, "other");
        auto beforeRename = Descriptor(), afterRename = Descriptor();
        ArtifactSources::Capture({{"model", source}}).Describe(beforeRename);
        ArtifactSources::Capture({{"model", renamed}}).Describe(afterRename);
        Require(beforeRename.Key() == afterRename.Key(), "artifact-rename-keeps-stable-id-and-content-key");

        auto store = std::make_shared<ArtifactStore>(root / "cache");
        const ArtifactData data{descriptor.Key(), std::string("binary\0payload", 14)};
        Require(store->Write(data), "artifact-cache-publishes-binary-payload");
        const auto loaded = store->Read(data.key);
        Require(loaded && loaded->bytes == data.bytes, "artifact-cache-round-trip");
        Write(root / "cache" / (data.key + ".artifact"), "corrupt");
        Require(!store->Read(data.key) && !store->Read("../outside"),
                "artifact-cache-rejects-corruption-and-invalid-key");
        Require(store->Write(data) && store->Read(data.key), "artifact-cache-recovers-from-corruption");

        ArtifactRequests requests(store);
        std::atomic<int> builds = 0;
        auto requestDescriptor = Descriptor();
        const auto first = requests.Request(requestDescriptor, {},
                                            [&]
                                            {
                                                ++builds;
                                                return "cooked";
                                            });
        const auto duplicate = requests.Request(requestDescriptor, {},
                                                [&]
                                                {
                                                    ++builds;
                                                    return "duplicate";
                                                });
        Wait(first);
        Wait(duplicate);
        Require(builds == 1 && first.Poll() == duplicate.Poll(), "artifact-coalesces-simultaneous-identical-requests");
        Require(requests.Publish(first) && requests.Published(requestDescriptor.assetId) == first.Poll(),
                "artifact-publishes-current-completed-generation");
        ArtifactRequests other;
        Require(!other.Publish(first), "artifact-rejects-another-owner-ticket");
        requestDescriptor.settings = "generation:old";
        auto gate = std::make_shared<std::promise<void>>();
        const auto released = gate->get_future().share();
        const auto old =
            requests.Request(requestDescriptor, {},
                             [released]
                             {
                                 if (released.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
                                     throw std::runtime_error("Fixture gate timed out");
                                 return "old";
                             });
        requestDescriptor.settings = "generation:new";
        const auto current = requests.Request(requestDescriptor, {},
                                              []
                                              {
                                                  return "new";
                                              });
        Wait(current);
        Require(requests.Publish(current), "artifact-new-generation-publishes-before-old-job-finishes");
        gate->set_value();
        Wait(old);
        Require(!requests.Publish(old) && requests.Published(requestDescriptor.assetId)->bytes == "new",
                "artifact-rejects-late-stale-job-result");
        requestDescriptor.settings = "generation:cancel";
        const auto cancelled = requests.Request(requestDescriptor, {},
                                                []
                                                {
                                                    return "cancelled";
                                                });
        requests.Cancel(requestDescriptor.assetId);
        Wait(cancelled);
        Require(!requests.Publish(cancelled) && requests.Published(requestDescriptor.assetId)->bytes == "new",
                "artifact-cancel-keeps-published-generation");
        requestDescriptor.settings = "generation:source-change";
        const auto changing = requests.Request(requestDescriptor, ArtifactSources::Capture({{"model", source}}),
                                               [&]
                                               {
                                                   Write(source, "during-import");
                                                   return "invalid";
                                               });
        Wait(changing);
        Require(!changing.Poll() && !changing.Error().empty() && !requests.Publish(changing),
                "artifact-rejects-input-change-during-import");
        requestDescriptor.settings = "generation:before-publication";
        const auto pending = requests.Request(requestDescriptor, ArtifactSources::Capture({{"model", source}}),
                                              []
                                              {
                                                  return "prepared";
                                              });
        Wait(pending);
        Write(source, "after-import");
        Require(pending.Poll() && !requests.Publish(pending), "artifact-rechecks-source-before-publication");
        Require(requests.Published(requestDescriptor.assetId)->bytes == "new",
                "artifact-failed-import-keeps-previous-published-lease");
        requestDescriptor.settings = "generation:retry";
        const auto failed = requests.Request(requestDescriptor, {},
                                             []() -> std::string
                                             {
                                                 throw std::runtime_error("injected conversion failure");
                                             });
        Wait(failed);
        Require(!failed.Poll() && !failed.Error().empty() && !requests.Publish(failed),
                "artifact-conversion-failure-is-diagnostic-and-unpublished");
        const auto retry = requests.Request(requestDescriptor, {},
                                            []
                                            {
                                                return "recovered";
                                            });
        Wait(retry);
        Require(retry.Poll() && requests.Publish(retry) &&
                    requests.Published(requestDescriptor.assetId)->bytes == "recovered",
                "artifact-same-input-retries-after-failed-job");
        const auto cachedPath = root / "cache" / (retry.Poll()->key + ".artifact");
        const auto retainedTime = std::filesystem::last_write_time(cachedPath) - std::chrono::seconds(60);
        std::filesystem::last_write_time(cachedPath, retainedTime);
        ArtifactRequests cached(store);
        const auto reused = cached.Request(requestDescriptor, {},
                                           []() -> std::string
                                           {
                                               throw std::runtime_error("cache should avoid conversion");
                                           });
        Wait(reused);
        Require(reused.Poll() && reused.Poll()->bytes == "recovered" && cached.Publish(reused),
                "artifact-new-owner-reuses-valid-cached-cpu-data");
        Require(std::filesystem::last_write_time(cachedPath) == retainedTime,
                "artifact-valid-cache-hit-does-not-rewrite-persistent-data");
        Require(!cached.Publish(retry), "artifact-same-key-ticket-still-belongs-to-original-owner");
    }
}
int main()
{
    try
    {
        const auto root = std::filesystem::absolute("generated/tests/artifacts") /
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        Validate(root);
        ModelInputs(root);
        ArtifactRequests draining;
        std::atomic_bool finished = false;
        const auto pending = draining.Request(Descriptor(), {},
                                              [&]
                                              {
                                                  std::this_thread::sleep_for(std::chrono::milliseconds(20));
                                                  finished = true;
                                                  return "drained";
                                              });
        draining.WaitForIdle();
        Require(finished && pending.Ready() && draining.Publish(pending),
                "artifact-owner-drains-pending-imports-before-cache-destruction");
        std::cout << "Artifact contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
