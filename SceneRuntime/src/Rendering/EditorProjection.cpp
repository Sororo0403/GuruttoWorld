#include <SceneRuntime/Rendering/EditorProjection.h>
#include <SceneRuntime/Rendering/RenderExtractor.h>
#include "SceneRenderBindings.h"

namespace SceneRuntime
{
    std::shared_ptr<const RenderPacket> EditorProjection::Prepare(const PreparedSceneResources& prepared,
                                                                  ID3D12Device* device, std::uint64_t generation,
                                                                  std::string& error) const
    {
        WorldState values(WorldDefinition::Compile(prepared.layout));
        std::vector<SceneAnimatorFrame> poses;
        for (const auto& [id, model] : prepared.animated)
        {
            if (!values.Read().Resolve(id))
                continue;
            SceneAnimatorFrame frame;
            frame.id = id;
            frame.model = model;
            if (!model->PreparePose(prepared.states.at(id).pose, frame.pose))
            {
                error = "Document display pose invalid";
                return {};
            }
            poses.push_back(std::move(frame));
        }
        SceneRenderBindings resources(values.Read(),
                                      {prepared.layout, prepared.objects, prepared.assetsRoot, prepared.assets,
                                       prepared.materials, prepared.animated, prepared.states, prepared.lods},
                                      poses, {device, generation, packet_, prepared.shadow, prepared.localShadow});
        return RenderExtractor::Extract(values.Read(), resources, generation, reinterpret_cast<std::uintptr_t>(device),
                                        {}, 0, error);
    }
}
