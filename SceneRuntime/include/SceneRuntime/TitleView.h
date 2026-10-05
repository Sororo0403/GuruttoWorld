#pragma once
#include <Engine/Graphics/Camera.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <algorithm>
#include <cmath>

namespace SceneRuntime::TitleView
{
    inline constexpr std::array<float, 5> Home{ -0.8f, 2.8f, -7.0f, 0.03f, 0.09f };
    inline void SetProjection(Engine::Camera& camera, float aspect)
    {
        const float fov=2.0f*std::atan(std::tan(DirectX::XM_PIDIV4*0.5f)*
            std::max(1.0f,(16.0f/9.0f)/aspect));
        camera.SetPerspective(fov,aspect,0.1f,220.0f);
    }
    inline void SetHome(Engine::Camera& camera)
    {
        camera.SetPosition({Home[0],Home[1],Home[2]});
        camera.SetRotation(Home[3],Home[4]);
    }
    inline Engine::DirectionalLight Light()
    {
        Engine::DirectionalLight light;
        light.direction={-0.5f,-0.8f,0.6f};
        light.color={1.0f,0.95f,0.84f};
        light.ambientIntensity=0.52f;
        light.intensity=0.76f;
        light.specularStrength=0.03f;
        return light;
    }
}
