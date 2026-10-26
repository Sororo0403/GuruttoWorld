#include <Engine/Graphics/Models/ModelAssets.h>
#include <Engine/Graphics/Models/ModelLoader.h>
#include "../../Assets/AssetImportCache.h"
#include "../../Assets/CpuAssetBinary.h"
#include <Engine/Assets/AssetPath.h>
#include <map>
#include <mutex>
#include <thread>
#include <cmath>

namespace
{
    using Meshes = std::vector<Engine::MeshData>;
    std::string Encode(const Meshes& meshes, const std::filesystem::path& source)
    {
        Engine::AssetBinary::Writer output;
        output.Pod(std::uint32_t{1});
        output.Count(meshes.size());
        for (const auto& mesh : meshes)
        {
            output.Array(mesh.vertices);
            output.Array(mesh.indices);
            output.Text(
                mesh.texturePath.empty()
                    ? std::string{}
                    : Engine::AssetPath::Text(
                          std::filesystem::absolute(mesh.texturePath).lexically_relative(source.parent_path())));
        }
        return std::move(output.bytes);
    }
    std::shared_ptr<const Meshes> Decode(std::string_view bytes, const std::filesystem::path& source)
    {
        Engine::AssetBinary::Reader input(bytes);
        if (input.Pod<std::uint32_t>() != 1)
            throw std::runtime_error("Unsupported CPU mesh artifact");
        auto meshes = std::make_shared<Meshes>();
        const auto count = input.Count();
        for (std::uint32_t index = 0; index < count; ++index)
        {
            Engine::MeshData mesh;
            mesh.vertices = input.Array<Engine::MeshVertex>();
            mesh.indices = input.Array<std::uint32_t>();
            const auto texture = input.Text();
            if (!texture.empty())
                mesh.texturePath = (source.parent_path() / Engine::AssetPath::Path(texture)).lexically_normal();
            if (mesh.indices.size() % 3 || mesh.vertices.empty())
                throw std::runtime_error("Invalid CPU mesh artifact");
            for (const auto vertex : mesh.indices)
                if (vertex >= mesh.vertices.size())
                    throw std::runtime_error("Invalid CPU mesh artifact index");
            for (const auto& vertex : mesh.vertices)
            {
                const auto finite = [](const auto& values)
                {
                    return std::all_of(values.begin(), values.end(),
                                       [](float value)
                                       {
                                           return std::isfinite(value);
                                       });
                };
                if (!finite(vertex.position) || !finite(vertex.normal) || !finite(vertex.uv) || !finite(vertex.color))
                    throw std::runtime_error("Non-finite CPU mesh artifact");
            }
            meshes->push_back(std::move(mesh));
        }
        input.End();
        if (meshes->empty())
            throw std::runtime_error("Empty CPU mesh artifact");
        return meshes;
    }
    std::mutex mutex;
    std::map<std::pair<std::string, std::filesystem::path>, std::weak_ptr<const Meshes>> leases;
}
namespace Engine
{
    bool ModelAssetRequest::Ready() const
    {
        return request_ && request_->ticket.Ready();
    }
    std::shared_ptr<const std::vector<MeshData>> ModelAssetRequest::Publish(std::string& error) const
    {
        try
        {
            const auto data = request_ ? request_->Publish(error) : nullptr;
            if (!data)
                return {};
            std::lock_guard lock(mutex);
            auto& lease = leases[{data->key, request_->source}];
            if (auto meshes = lease.lock())
                return meshes;
            auto meshes = Decode(data->bytes, request_->source);
            lease = meshes;
            std::erase_if(leases,
                          [](const auto& item)
                          {
                              return item.second.expired();
                          });
            return meshes;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return {};
        }
    }
    ModelAssetRequest ModelAssets::Request(const std::filesystem::path& path)
    {
        ModelAssetRequest result;
        result.request_ =
            RequestAssetImport(path, "static-mesh",
                               [](const std::filesystem::path& source)
                               {
                                   std::vector<MeshData> meshes;
                                   if (!ModelLoader::Load(source, meshes))
                                       throw std::runtime_error("CPU model import failed: " + AssetPath::Text(source));
                                   return Encode(meshes, source);
                               });
        return result;
    }
    std::shared_ptr<const std::vector<MeshData>> ModelAssets::Load(const std::filesystem::path& path,
                                                                   std::string& error)
    {
        try
        {
            const auto request = Request(path);
            while (!request.Ready())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return request.Publish(error);
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return {};
        }
    }
    bool ModelAssets::Cook(const std::filesystem::path& path, const std::filesystem::path& directory,
                           std::string& error)
    {
        try
        {
            return Request(path).request_->Cook(directory, error);
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
}
