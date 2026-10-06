#pragma once
#include <SceneRuntime/SceneView.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace AuthoredViewFixture
{
    inline SceneRuntime::ScenePlacement Camera()
    {
        const auto layout=SceneRuntime::SceneLayout::Load("Content/Assets/Scenes/TitleStreet.json");
        const auto* placement=SceneRuntime::SceneView::CameraObject(layout);
        if (!placement) throw std::runtime_error("title fixture requires an authored camera");
        return *placement;
    }
    inline void SetHome(Engine::Camera& camera)
    {
        const auto placement=Camera();
        camera.SetPosition(placement.position);
        camera.SetRotation(placement.rotation[1],-placement.rotation[0]);
    }
    inline void SetProjection(Engine::Camera& camera, float aspect)
    {
        const auto settings=*Camera().camera;
        float fov=settings.verticalFov*DirectX::XM_PI/180;
        if (settings.preserveHorizontal) fov=2*std::atan(std::tan(fov*0.5f)*std::max(1.0f,settings.referenceAspect/aspect));
        camera.SetPerspective(fov,aspect,settings.nearClip,settings.farClip);
    }
    inline Engine::DirectionalLight Light()
    {
        const auto layout=SceneRuntime::SceneLayout::Load("Content/Assets/Scenes/TitleStreet.json");
        const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[](const auto& placement) { return placement.directionalLight.has_value(); });
        if (found==layout.objects.end()) throw std::runtime_error("title fixture requires authored lighting");
        const auto& settings=*found->directionalLight;
        Engine::DirectionalLight light;
        light.direction=settings.direction; light.color=settings.color; light.intensity=settings.intensity;
        light.ambientIntensity=settings.ambient; light.specularStrength=settings.specular; light.shininess=settings.shininess;
        const auto& fog=layout.settings.fog; light.fog={fog.enabled,fog.color,fog.start,fog.end,fog.strength};
        return light;
    }
}
