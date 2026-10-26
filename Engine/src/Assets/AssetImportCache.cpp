#include "AssetImportCache.h"
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Core/DiagnosticPaths.h>
#include <mutex>
#include <thread>
#include <semaphore>

namespace Engine
{
    namespace
    {
        struct ImportOwners
        {
            // 共有所有者より後に実行枠を破棄します。
            std::counting_semaphore<4> workers{4};
            std::mutex mutex;
            std::map<std::pair<std::filesystem::path, std::string>, std::shared_ptr<ArtifactRequests>> owners;
        };
        ImportOwners& Owners()
        {
            static ImportOwners owners;
            return owners;
        }
    }
    AssetImportScope::~AssetImportScope()
    {
        auto& imports = Owners();
        std::lock_guard lock(imports.mutex);
        for (const auto& [key, owner] : imports.owners)
        {
            static_cast<void>(key);
            owner->WaitForIdle();
        }
    }
    std::shared_ptr<AssetImportRequest> RequestAssetImport(
        const std::filesystem::path& path, const std::string& kind,
        std::function<std::string(const std::filesystem::path&)> build,
        std::function<void(const std::filesystem::path&, ArtifactDescriptor&, ArtifactSources&)> describe)
    {
        auto request = std::make_shared<AssetImportRequest>();
        request->source = std::filesystem::absolute(path).lexically_normal();
        auto root = AssetDatabase::Root(request->source);
        if (!root.empty())
            request->source = root / AssetDatabase(root, true).Resolve(request->source.lexically_relative(root));
        else
            root = request->source.parent_path();
        if (describe)
            describe(request->source, request->descriptor, request->sources);
        else
        {
            auto inputs = ModelImportInputs::Capture(request->source, kind);
            request->descriptor = std::move(inputs.descriptor);
            request->sources = std::move(inputs.sources);
        }
        if (request->descriptor.assetId.empty())
            request->descriptor.assetId = "path:" + AssetPath::Text(request->source.lexically_relative(root));
        auto& imports = Owners();
        {
            std::lock_guard lock(imports.mutex);
            auto& owner = imports.owners[{root, kind}];
            if (!owner)
            {
                const auto diagnostics = GetDiagnosticsRoot();
                const auto local = diagnostics.empty() ? std::filesystem::path{} : diagnostics / "artifacts" / kind;
                owner = std::make_shared<ArtifactRequests>(
                    std::make_shared<const ArtifactStore>(local, root / "Assets/Compiled" / kind), false);
            }
            request->owner = owner;
        }
        request->ticket = request->owner->Request(request->descriptor, request->sources,
                                                  [source = request->source, build = std::move(build)]
                                                  {
                                                      auto& workers = Owners().workers;
                                                      workers.acquire();
                                                      struct Worker
                                                      {
                                                          ~Worker()
                                                          {
                                                              Owners().workers.release();
                                                          }
                                                      } worker;
                                                      return build(source);
                                                  });
        return request;
    }
    std::shared_ptr<const ArtifactData> AssetImportRequest::Publish(std::string& error) const
    {
        auto data = ticket.Poll();
        if (!data || !owner->Publish(ticket))
        {
            error = ticket.Error().empty() ? "Asset import request is pending or obsolete" : ticket.Error();
            return {};
        }
        error.clear();
        return data;
    }
    bool AssetImportRequest::Cook(const std::filesystem::path& directory, std::string& error) const
    {
        try
        {
            const auto output = std::filesystem::absolute(directory).lexically_normal();
            while (!ticket.Ready())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const auto data = Publish(error);
            if (!data)
                return false;
            if (data->bytes.size() > ArtifactStore::MaximumBytes)
            {
                error.clear();
                return true;
            }
            auto recipeDescriptor = descriptor;
            sources.Describe(recipeDescriptor);
            if (recipeDescriptor.Key() != data->key || !ArtifactStore(output).Write(*data))
                throw std::runtime_error("Cannot write cooked CPU artifact");
            auto keyInputs = Json::array(), inputs = Json::array();
            for (const auto& input : recipeDescriptor.dependencies)
                keyInputs.push_back({{"id", input.id}, {"revision", input.revision}});
            for (const auto& input : sources.Inputs())
                inputs.push_back({{"path", AssetPath::Text(input.path.lexically_relative(output))},
                                  {"sha256", input.revision.value_or("missing")},
                                  {"keyInput", input.keyInput}});
            const Json recipe{{"version", 1},
                              {"key", data->key},
                              {"kind", descriptor.kind},
                              {"assetId", descriptor.assetId},
                              {"sourceRevision", descriptor.sourceRevision},
                              {"importerVersion", descriptor.importerVersion},
                              {"settings", descriptor.settings},
                              {"platform", descriptor.platform},
                              {"compiler", descriptor.compiler},
                              {"keyInputs", keyInputs},
                              {"inputBase", "."},
                              {"inputs", inputs},
                              {"outputRevision", ArtifactHash::Bytes(data->bytes)}};
            FileContents::Write(output / (data->key + ".recipe.json"), recipe.dump(2));
            if (!sources.Unchanged())
                throw std::runtime_error("CPU artifact inputs changed during cook");
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
}
