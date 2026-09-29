#include <Engine/Graphics/Camera.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <Engine/Graphics/Models/ParticleSystem.h>
#include <Engine/Graphics/Resources/TextureManager.h>
#include <dxgi1_6.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        Engine::Camera camera;
        Check(camera.SetPosition({ 1, 2, -3 }), "camera position");
        Check(camera.SetRotation(0, 0), "camera rotation");
        DirectX::XMFLOAT4X4 view;
        DirectX::XMStoreFloat4x4(&view, camera.GetViewMatrix());
        Check(view._41 == -1 && view._42 == -2 && view._43 == 3, "view follows position");
        Check(!camera.SetPosition({ std::numeric_limits<float>::infinity(), 0, 0 }), "invalid camera position");
        Check(camera.GetPosition()[0] == 1, "invalid position preserves camera");
        Check(camera.SetPerspective(DirectX::XM_PIDIV4, 2, 0.1f, 100), "perspective");
        DirectX::XMFLOAT4X4 projection;
        DirectX::XMStoreFloat4x4(&projection, camera.GetProjectionMatrix());
        Check(std::abs(projection._22 / projection._11 - 2) < 0.001f, "aspect ratio");
        Check(!camera.SetAspectRatio(0) && !camera.SetPerspective(1, 1, 2, 1), "invalid projection");
        Engine::Object3D first, second;
        first.Draw(nullptr, camera);
        second.Draw(nullptr, camera);

        using Microsoft::WRL::ComPtr;
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        Check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "factory");
        Check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))), "WARP");
        Check(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))), "device");
        D3D12_COMMAND_QUEUE_DESC desc{};
        Check(SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))), "queue");
        Engine::TextureManager textures;
        Check(textures.Initialize(device.Get(), queue.Get()), "textures");
        auto checker = textures.Load("App/Assets/Textures/Checker.png");
        auto white = textures.Load({});
        Check(checker && white && checker != white, "different textures");
        Engine::ParticleSystem particles;
        const auto shader = std::filesystem::path("App/Shaders/Particle.hlsl");
        Check(particles.CreateGroup("Checker", device.Get(), queue.Get(), checker, shader), "checker group");
        Check(particles.CreateGroup("Glow", device.Get(), queue.Get(), white, shader), "glow group");
        Check(!particles.CreateGroup("Glow", device.Get(), queue.Get(), white, shader), "duplicate group rejected");
        Check(!particles.CreateGroup("Invalid", device.Get(), queue.Get(), {}, shader), "invalid texture rejected");
        Engine::Particle particle;
        particle.position = { 1, 2, 3 };
        particle.velocity = { 0, 1, 0 };
        particle.lifetime = 1;
        Check(particles.Emit("Checker", particle), "emit first group");
        particle.lifetime = 2;
        Check(particles.Emit("Glow", particle), "emit second group");
        Check(!particles.Emit("Unknown", particle), "unknown group rejected");
        particle.position[0] = std::numeric_limits<float>::quiet_NaN();
        Check(!particles.Emit("Glow", particle), "invalid position rejected");
        particles.Update(-1);
        particles.Update(std::numeric_limits<double>::quiet_NaN());
        Check(particles.GetParticleCount("Checker") == 1, "invalid update ignored");
        particles.Update(1);
        Check(particles.GetParticleCount("Checker") == 0 && particles.GetParticleCount("Glow") == 1, "independent group lifetimes");
        particles.Update(1);
        Check(particles.GetParticleCount("Glow") == 0, "expired removed");
        particle = {};
        for (int index = 0; index < 1024; ++index) Check(particles.Emit("Glow", particle), "emit within capacity");
        Check(!particles.Emit("Glow", particle), "capacity enforced");
        particles.Update(2);
        Check(particles.Emit("Glow", particle), "capacity recycled");
        particles.Draw(nullptr, camera);
        std::cout << "PASS: camera transforms/projection, shared camera API, texture groups, emit validation, lifetime and capacity\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
