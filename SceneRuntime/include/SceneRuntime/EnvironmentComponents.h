#pragma once
#include <array>
#include <string>

namespace SceneRuntime
{
    struct CameraComponent
    {
        std::string id="camera";
        bool enabled=true;
        float verticalFov=45;
        float nearClip=0.1f;
        float farClip=220;
        float referenceAspect=16.0f/9.0f;
        bool preserveHorizontal=true;
        bool operator==(const CameraComponent&) const = default;
    };
    struct DirectionalLightComponent
    {
        std::string id="light";
        bool enabled=true;
        std::array<float,3> direction{0,-1,1};
        std::array<float,3> color{1,1,1};
        float intensity=0.8f;
        float ambient=0.2f;
        float specular=0.4f;
        float shininess=32;
        bool shadowsEnabled=false;
        float shadowDistance=70,shadowBias=.0001f;
        bool operator==(const DirectionalLightComponent&) const = default;
    };
    struct CloudBank
    {
        std::array<float,2> center{0.5f,0.25f};
        float size=1;
        bool operator==(const CloudBank&) const = default;
    };
    struct SkyComponent
    {
        std::string id="sky";
        bool enabled=true;
        std::array<float,3> horizon{0.76f,0.84f,0.85f};
        std::array<float,3> zenith{0.30f,0.57f,0.77f};
        float horizonHeight=0.66f;
        std::array<float,3> sunColor{1,0.95f,0.79f};
        std::array<float,2> sunCenter{0.78f,0.13f};
        std::array<float,2> sunRadius{0.22f,0.27f};
        float sunStrength=0.42f;
        std::array<float,3> cloudLow{0.88f,0.92f,0.94f};
        std::array<float,3> cloudHigh{1,0.98f,0.91f};
        float cloudOpacity=0.86f;
        std::array<CloudBank,3> clouds{{{{0.20f,0.24f},1.05f},{{0.51f,0.12f},0.85f},{{0.90f,0.31f},1.20f}}};
        std::array<float,2> cloudVelocity{}; // UV per second; zero keeps clouds stationary.
        float referenceAspect=16.0f/9.0f;
        bool operator==(const SkyComponent&) const = default;
    };
    struct ParticleEmitterComponent
    {
        std::string id="particles";
        bool enabled=true;
        unsigned int count=24;
        std::array<float,4> color{1,0.92f,0.65f,0.32f};
        float size=0.075f;
        std::array<float,3> extent{4,0,16.8f};
        std::array<float,3> travel{0,3,0};
        float cycle=12;
        float drift=0.18f;
        bool operator==(const ParticleEmitterComponent&) const = default;
    };
    struct CameraSwayComponent
    {
        std::string id="sway";
        bool enabled=true;
        std::array<float,3> amplitude{0.10f,0.04f,0};
        std::array<float,3> period{24,40,24};
        bool operator==(const CameraSwayComponent&) const = default;
    };
}
