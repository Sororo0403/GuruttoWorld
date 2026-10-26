#include <SceneRuntime/Rendering/SceneView.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace SceneRuntime
{
    bool SceneView::Camera(const RenderPacket& packet, float aspect, double seconds, Engine::Camera& camera)
    {
        const auto found =
            std::find_if(packet.cameras.begin(), packet.cameras.end(),
                         [&](const auto& camera)
                         {
                             return packet.settings.mainCamera.empty() || camera.id == packet.settings.mainCamera;
                         });
        if (found == packet.cameras.end() || !std::isfinite(aspect) || aspect <= 0 || !std::isfinite(seconds))
            return false;
        const auto& transform = found->matrix;
        const auto& rotation = found->rotation;
        std::array<float, 3> offset{};
        const auto sway = std::find_if(packet.cameraSways.begin(), packet.cameraSways.end(),
                                       [&](const auto& node)
                                       {
                                           return node.entity == found->entity;
                                       });
        if (sway != packet.cameraSways.end())
            for (size_t axis = 0; axis < 3; ++axis)
                offset[axis] =
                    sway->settings.amplitude[axis] *
                    static_cast<float>(std::sin(seconds * 2 * std::numbers::pi / sway->settings.period[axis]));
        const auto matrix = DirectX::XMLoadFloat4x4(&rotation);
        DirectX::XMFLOAT3 movement;
        DirectX::XMStoreFloat3(&movement, DirectX::XMVector3TransformNormal(
                                              DirectX::XMVectorSet(offset[0], offset[1], offset[2], 0), matrix));
        auto candidate = camera;
        if (!candidate.SetPosition(
                {transform._41 + movement.x, transform._42 + movement.y, transform._43 + movement.z}) ||
            !candidate.SetOrientation({rotation._31, rotation._32, rotation._33},
                                      {rotation._21, rotation._22, rotation._23}))
            return false;
        const auto& config = found->settings;
        float fov = config.verticalFov * std::numbers::pi_v<float> / 180;
        if (config.preserveHorizontal)
            fov = 2 * std::atan(std::tan(fov * 0.5f) * std::max(1.0f, config.referenceAspect / aspect));
        fov = std::min(fov, DirectX::XM_PI - 0.002f);
        if (!candidate.SetPerspective(fov, aspect, config.nearClip, config.farClip))
            return false;
        camera = candidate;
        return true;
    }
    Engine::DirectionalLight SceneView::Light(const RenderPacket& packet)
    {
        Engine::DirectionalLight result;
        const auto& fog = packet.settings.fog;
        result.fog = {fog.enabled, fog.color, fog.start, fog.end, fog.strength};
        result.intensity = 0;
        result.ambientIntensity = 0;
        result.specularStrength = 0;
        if (!packet.directionalLights.empty())
        {
            const auto& found = packet.directionalLights.front();
            const auto& config = found.settings;
            result.color = config.color;
            result.intensity = config.intensity;
            result.ambientIntensity = config.ambient;
            result.specularStrength = config.specular;
            result.shininess = config.shininess;
            result.shadowsEnabled = config.shadowsEnabled;
            result.shadowDistance = config.shadowDistance;
            result.shadowBias = config.shadowBias;
            DirectX::XMFLOAT3 direction;
            DirectX::XMStoreFloat3(
                &direction, DirectX::XMVector3TransformNormal(
                                DirectX::XMVectorSet(config.direction[0], config.direction[1], config.direction[2], 0),
                                DirectX::XMLoadFloat4x4(&found.rotation)));
            result.direction = {direction.x, direction.y, direction.z};
        }
        for (const auto& index : packet.localLights)
        {
            Engine::LocalLight light;
            if (!index.spot)
            {
                const auto& node = packet.pointLights.at(index.index);
                light.position = {node.matrix._41, node.matrix._42, node.matrix._43};
                light.color = node.settings.color;
                light.intensity = node.settings.intensity;
                light.range = node.settings.range;
                light.shadowCount = node.settings.shadowsEnabled ? 6.0f : 0.0f;
            }
            else
            {
                const auto& node = packet.spotLights.at(index.index);
                light.position = {node.matrix._41, node.matrix._42, node.matrix._43};
                light.direction = {node.rotation._31, node.rotation._32, node.rotation._33};
                light.spot = 1;
                light.color = node.settings.color;
                light.intensity = node.settings.intensity;
                light.range = node.settings.range;
                constexpr float radians = std::numbers::pi_v<float> / 360;
                light.innerCosine = std::cos(node.settings.innerAngle * radians);
                light.outerCosine = std::cos(node.settings.outerAngle * radians);
                light.shadowCount = node.settings.shadowsEnabled ? 1.0f : 0.0f;
            }
            result.localLights.push_back(light);
        }
        return result;
    }
}
