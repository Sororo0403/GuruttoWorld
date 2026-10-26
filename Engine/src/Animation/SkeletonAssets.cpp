#include <Engine/Animation/SkeletonAssets.h>
#include "../Assets/CpuAssetBinary.h"
#include "../Assets/AssetImportCache.h"
#include <Engine/Assets/ModelImportInputs.h>
#include <Engine/Assets/AssetPath.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Core/DiagnosticPaths.h>
#include <cstring>
#include <mutex>
#include <thread>
#include <type_traits>

namespace
{
    using Engine::AssetBinary::Reader;
    using Engine::AssetBinary::Writer;
    std::string Encode(const Engine::SkeletonData& rig, const std::filesystem::path& source)
    {
        Writer output;
        output.Pod(std::uint32_t{2});
        output.Pod(rig.inverseRoot);
        output.Pod(rig.importScale);
        output.Count(rig.nodes.size());
        for (const auto& node : rig.nodes)
        {
            output.Text(node.name);
            output.Pod(static_cast<std::int32_t>(node.parent));
            output.Pod(node.rest);
        }
        output.Count(rig.meshes.size());
        for (const auto& mesh : rig.meshes)
        {
            output.Array(mesh.mesh.vertices);
            output.Array(mesh.mesh.indices);
            output.Text(
                mesh.mesh.texturePath.empty()
                    ? std::string{}
                    : Engine::AssetPath::Text(
                          std::filesystem::absolute(mesh.mesh.texturePath).lexically_relative(source.parent_path())));
            output.Pod(static_cast<std::uint64_t>(mesh.node));
            output.Count(mesh.joints.size());
            for (const auto joint : mesh.joints)
                output.Pod(static_cast<std::uint64_t>(joint));
            output.Array(mesh.inverseBind);
            output.Array(mesh.weights);
        }
        output.Count(rig.clips.size());
        for (const auto& clip : rig.clips)
        {
            output.Text(clip.name);
            output.Pod(clip.duration);
            output.Count(clip.tracks.size());
            for (const auto& [node, track] : clip.tracks)
            {
                output.Pod(static_cast<std::uint64_t>(node));
                output.Array(track.positions);
                output.Array(track.scales);
                output.Array(track.rotations);
            }
        }
        return std::move(output.bytes);
    }
    std::shared_ptr<const Engine::SkeletonData> Decode(std::string_view bytes, const std::filesystem::path& source)
    {
        Reader input(bytes);
        if (input.Pod<std::uint32_t>() != 2)
            throw std::runtime_error("Unsupported skeleton artifact");
        auto rig = std::make_shared<Engine::SkeletonData>();
        rig->inverseRoot = input.Pod<DirectX::XMFLOAT4X4>();
        rig->importScale = input.Pod<float>();
        const auto nodes = input.Count();
        for (std::uint32_t index = 0; index < nodes; ++index)
        {
            Engine::SkeletonNode node;
            node.name = input.Text();
            node.parent = input.Pod<std::int32_t>();
            node.rest = input.Pod<Engine::BonePose>();
            rig->nodes.push_back(std::move(node));
        }
        const auto meshes = input.Count();
        for (std::uint32_t index = 0; index < meshes; ++index)
        {
            Engine::RiggedMesh mesh;
            mesh.mesh.vertices = input.Array<Engine::MeshVertex>();
            mesh.mesh.indices = input.Array<std::uint32_t>();
            const auto texture = input.Text();
            if (!texture.empty())
                mesh.mesh.texturePath = (source.parent_path() / Engine::AssetPath::Path(texture)).lexically_normal();
            mesh.node = input.Pod<std::uint64_t>();
            const auto joints = input.Count();
            for (std::uint32_t joint = 0; joint < joints; ++joint)
                mesh.joints.push_back(input.Pod<std::uint64_t>());
            mesh.inverseBind = input.Array<DirectX::XMFLOAT4X4>();
            mesh.weights = input.Array<Engine::SkinWeights>();
            rig->meshes.push_back(std::move(mesh));
        }
        const auto clips = input.Count();
        for (std::uint32_t index = 0; index < clips; ++index)
        {
            Engine::SkeletalClip clip;
            clip.name = input.Text();
            clip.duration = input.Pod<float>();
            const auto tracks = input.Count();
            for (std::uint32_t trackIndex = 0; trackIndex < tracks; ++trackIndex)
            {
                const auto node = input.Pod<std::uint64_t>();
                Engine::BoneTrack track;
                track.positions = input.Array<Engine::BoneKey<3>>();
                track.scales = input.Array<Engine::BoneKey<3>>();
                track.rotations = input.Array<Engine::BoneKey<4>>();
                if (!clip.tracks.emplace(node, std::move(track)).second)
                    throw std::runtime_error("Duplicate skeleton artifact track");
            }
            rig->clips.push_back(std::move(clip));
        }
        input.End();
        const auto matrices = Engine::Skeleton::Matrices(*rig, Engine::Skeleton::Sample(*rig, "", 0, false));
        for (const auto& mesh : rig->meshes)
            static_cast<void>(Engine::Skeleton::Skin(*rig, mesh, matrices));
        return rig;
    }
    struct Assets
    {
        std::mutex mutex;
        std::map<std::pair<std::string, std::filesystem::path>, std::weak_ptr<const Engine::SkeletonData>> leases;
    };
    Assets& Shared()
    {
        static Assets assets;
        return assets;
    }
}
namespace Engine
{
    bool SkeletonAssetRequest::Ready() const
    {
        return request_ && request_->ticket.Ready();
    }
    std::shared_ptr<const SkeletonData> SkeletonAssetRequest::Publish(std::string& error) const
    {
        try
        {
            auto& assets = Shared();
            const auto data = request_ ? request_->Publish(error) : nullptr;
            if (!data)
                return {};
            std::lock_guard lock(assets.mutex);
            const auto key = std::make_pair(data->key, request_->source);
            auto& lease = assets.leases[key];
            if (auto rig = lease.lock())
            {
                error.clear();
                return rig;
            }
            auto rig = Decode(data->bytes, request_->source);
            lease = rig;
            std::erase_if(assets.leases,
                          [](const auto& item)
                          {
                              return item.second.expired();
                          });
            error.clear();
            return rig;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return {};
        }
    }
    SkeletonAssetRequest SkeletonAssets::Request(const std::filesystem::path& path)
    {
        SkeletonAssetRequest result;
        result.request_ = RequestAssetImport(path, "skeletal-mesh",
                                             [](const std::filesystem::path& source)
                                             {
                                                 std::string error;
                                                 const auto rig = Skeleton::Load(source, error);
                                                 if (!rig)
                                                     throw std::runtime_error(error);
                                                 return Encode(*rig, source);
                                             });
        return result;
    }
    bool SkeletonAssets::Cook(const std::filesystem::path& path, const std::filesystem::path& directory,
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
    bool SkeletonAssets::SameArtifact(const std::shared_ptr<const SkeletonData>& first,
                                      const std::shared_ptr<const SkeletonData>& second)
    {
        if (first == second)
            return true;
        auto& assets = Shared();
        std::lock_guard lock(assets.mutex);
        std::string a, b;
        for (const auto& [key, lease] : assets.leases)
        {
            const auto rig = lease.lock();
            if (rig == first)
                a = key.first;
            if (rig == second)
                b = key.first;
        }
        return !a.empty() && a == b;
    }
    std::shared_ptr<const SkeletonData> SkeletonAssets::Load(const std::filesystem::path& path, std::string& error)
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
}
