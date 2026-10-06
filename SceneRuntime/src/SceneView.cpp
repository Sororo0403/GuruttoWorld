#include <SceneRuntime/SceneView.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace SceneRuntime
{
    const ScenePlacement* SceneView::CameraObject(const SceneLayout& layout)
    {
        const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[&](const auto& object) {
            return object.camera && object.camera->enabled &&
                (layout.settings.mainCamera.empty() || object.id==layout.settings.mainCamera);
        });
        return found==layout.objects.end() ? nullptr : &*found;
    }
    bool SceneView::Camera(const SceneWorld& world, float aspect, double seconds, Engine::Camera& camera)
    {
        const auto* placement=CameraObject(world.Layout());
        if (!placement) return false;
        DirectX::XMFLOAT4X4 transform,rotation;
        if (!world.WorldMatrix(placement->id,transform) || !world.WorldRotation(placement->id,rotation)) return false;
        std::array<float,3> offset{};
        if (placement->cameraSway && placement->cameraSway->enabled)
            for (size_t axis=0;axis<3;++axis)
                offset[axis]=placement->cameraSway->amplitude[axis]*static_cast<float>(std::sin(
                    seconds*2*std::numbers::pi/placement->cameraSway->period[axis]));
        const auto matrix=DirectX::XMLoadFloat4x4(&rotation);
        DirectX::XMFLOAT3 movement;
        DirectX::XMStoreFloat3(&movement,DirectX::XMVector3TransformNormal(
            DirectX::XMVectorSet(offset[0],offset[1],offset[2],0),matrix));
        auto candidate=camera;
        if (!candidate.SetPosition({transform._41+movement.x,transform._42+movement.y,transform._43+movement.z}) ||
            !candidate.SetOrientation({rotation._31,rotation._32,rotation._33},{rotation._21,rotation._22,rotation._23})) return false;
        const auto& config=*placement->camera;
        float fov=config.verticalFov*std::numbers::pi_v<float>/180;
        if (config.preserveHorizontal) fov=2*std::atan(std::tan(fov*0.5f)*std::max(1.0f,config.referenceAspect/aspect));
        fov=std::min(fov,DirectX::XM_PI-0.002f);
        if (!candidate.SetPerspective(fov,aspect,config.nearClip,config.farClip)) return false;
        camera=candidate;
        return true;
    }
    Engine::DirectionalLight SceneView::Light(const SceneWorld& world)
    {
        Engine::DirectionalLight result;
        const auto& fog=world.Layout().settings.fog;
        result.fog={fog.enabled,fog.color,fog.start,fog.end,fog.strength};
        result.intensity=0; result.ambientIntensity=0; result.specularStrength=0;
        const auto& objects=world.Layout().objects;
        const auto found=std::find_if(objects.begin(),objects.end(),[](const auto& object) {
            return object.directionalLight && object.directionalLight->enabled;
        });
        if (found==objects.end()) return result;
        const auto& config=*found->directionalLight;
        result.color=config.color; result.intensity=config.intensity; result.ambientIntensity=config.ambient;
        result.specularStrength=config.specular; result.shininess=config.shininess;
        DirectX::XMFLOAT4X4 rotation;
        if (world.WorldRotation(found->id,rotation))
        {
            DirectX::XMFLOAT3 direction;
            DirectX::XMStoreFloat3(&direction,DirectX::XMVector3TransformNormal(
                DirectX::XMVectorSet(config.direction[0],config.direction[1],config.direction[2],0),DirectX::XMLoadFloat4x4(&rotation)));
            result.direction={direction.x,direction.y,direction.z};
        }
        return result;
    }
}
