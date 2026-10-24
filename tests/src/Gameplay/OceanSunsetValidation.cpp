#include <SceneRuntime/Scene/SceneEnvironment.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <Engine/Platform/Window.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <limits>
#include <chrono>
#include <cmath>

namespace
{
    void Require(bool success, const std::string& message)
    {
        if (!success)
            throw std::runtime_error(message);
    }
    std::vector<unsigned char> Capture(Engine::DirectX12Renderer& renderer, SceneRuntime::SceneEnvironment& scene,
                                       const std::string& name, UINT width = 1280, UINT height = 720,
                                       const Engine::Camera* camera = nullptr)
    {
        const UINT pitch = (width * 4 + 255) & ~255U;
        Engine::RenderTexture target;
        Require(target.Resize(renderer, width, height), "Capture target");
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<UINT64>(pitch) * height;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        Require(SUCCEEDED(renderer.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                        IID_PPV_ARGS(&readback))),
                "Readback buffer");
        Require(renderer.Render({0, 0, 0, 1},
                                [&](ID3D12GraphicsCommandList* commands, float)
                                {
                                    Require(target.Begin(commands, {0, 0, 0, 1}), "Capture begin");
                                    scene.Draw(commands, width, height, camera);
                                    Require(target.End(commands), "Capture end");
                                    D3D12_RESOURCE_BARRIER barrier{};
                                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                                    barrier.Transition = {target.GetResource(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                          D3D12_RESOURCE_STATE_COPY_SOURCE};
                                    commands->ResourceBarrier(1, &barrier);
                                    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
                                    source.pResource = target.GetResource();
                                    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                                    destination.pResource = readback.Get();
                                    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                                    destination.PlacedFootprint.Footprint = {Engine::RenderTexture::Format, width,
                                                                             height, 1, pitch};
                                    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
                                    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                                    commands->ResourceBarrier(1, &barrier);
                                }) != Engine::RenderResult::Failed &&
                    renderer.WaitForIdle(),
                "Capture GPU complete");
        unsigned char* mapped = nullptr;
        const D3D12_RANGE range{0, static_cast<SIZE_T>(pitch) * height};
        Require(SUCCEEDED(readback->Map(0, &range, reinterpret_cast<void**>(&mapped))), "Map pixels");
        std::vector<unsigned char> pixels;
        pixels.reserve(static_cast<size_t>(width) * height * 3);
        for (UINT y = 0; y < height; ++y)
            for (UINT x = 0; x < width; ++x)
                pixels.insert(pixels.end(), mapped + y * pitch + x * 4, mapped + y * pitch + x * 4 + 3);
        const D3D12_RANGE empty{0, 0};
        readback->Unmap(0, &empty);
        std::filesystem::create_directories("generated/ocean-sunset/previews");
        std::ofstream file("generated/ocean-sunset/previews/" + name + ".ppm", std::ios::binary);
        file << "P6\n" << width << " " << height << "\n255\n";
        file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        Require(static_cast<bool>(file), "Save pixels");
        return pixels;
    }
}
int main()
{
    try
    {
        using namespace SceneRuntime;
        const auto root = std::filesystem::absolute("Games/GuruttoWorld");
        const auto authored = SceneLayout::Load(root / "Assets/Scenes/OceanSunset.json");
        authored.Validate();
        Require(SceneLayout::Parse(authored.Serialize()).Serialize() == authored.Serialize(), "Ocean roundtrip");
        for (const auto& name : {"TitleStreet.json", "ParkAdventure.json", "RenderingPlayground.json"})
        {
            const auto legacy = SceneLayout::Load(root / "Assets/Scenes" / name);
            const auto serialized = legacy.Serialize();
            Require(serialized.find("\"ocean\"") == std::string::npos, "Legacy sky has no ocean mode");
            Require(SceneLayout::Parse(serialized).Serialize() == serialized, "Legacy scene roundtrip");
        }
        for (float value : {0.0f, std::numeric_limits<float>::quiet_NaN()})
        {
            auto invalid = authored;
            invalid.objects[1].sky->ocean->refractiveIndex = value;
            bool rejected = false;
            try
            {
                invalid.Validate();
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            Require(rejected, "Invalid ocean optical parameter rejected");
        }
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Require(window.Create(L"Ocean sunset validation", 1280, 720) && renderer.Initialize(window.GetHandle()),
                "D3D12 initialize");
        {
            auto texture = std::make_shared<Engine::Texture2D>();
            Require(texture->Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), {}), "Depth fixture texture");
            Engine::SpriteRenderer plain, readOnly, writable, shared, invalid;
            Require(plain.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), texture,
                                     root / "Shaders/Sprite.hlsl"),
                    "Plain sprite fixture");
            Require(readOnly.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), texture,
                                        root / "Shaders/Sprite.hlsl", true),
                    "Read-only depth fixture");
            Require(writable.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), texture,
                                        root / "Shaders/Sprite.hlsl", true, true),
                    "Writable depth fixture");
            Require(!plain.SharesPipelineWith(readOnly) && !readOnly.SharesPipelineWith(writable),
                    "Depth write mode isolates pipeline cache");
            Require(shared.InitializeShared(writable, texture) && shared.SharesPipelineWith(writable),
                    "Shared sprite retains depth state");
            Require(!invalid.Initialize(renderer.GetDevice(), renderer.GetCommandQueue(), texture,
                                        root / "Shaders/Sprite.hlsl", false, true),
                    "Depth writes require depth testing");
        }
        SceneEnvironment scene;
        std::string error;
        Require(scene.Initialize(renderer, root, authored, error), error);
        const auto runtimeBaseline = scene.World().Layout().Serialize();
        Require(scene.StartAudio(root, error), error);
        scene.UpdateAudio(true);
        const auto first = Capture(renderer, scene, "sunset");
        scene.SeekAnimation(9, 9);
        const auto second = Capture(renderer, scene, "sunset-9s");
        Require(first != second, "Waves and cloud motion change rendered image");
        scene.Update(.5, false, true);
        Require(Capture(renderer, scene, "motion-stopped", 1280, 720) == second,
                "Disabled motion keeps ray-rendered image fixed");
        Engine::Camera camera;
        Require(SceneView::Camera(scene.World(), 16.0f / 9.0f, 9, camera), "Authored camera");
        Require(camera.SetRotation(0, 0), "Editor camera rotation");
        const auto turned = Capture(renderer, scene, "editor-north", 640, 360, &camera);
        const auto baseline = Capture(renderer, scene, "game-small", 640, 360);
        Require(turned != baseline, "Scene-view camera affects world-space sky and reflections");
        auto editable = authored;
        editable.settings.oceanShader = "Shaders/OceanSunset.hlsl";
        editable.settings.skyShader = "Shaders/Sky.hlsl";
        editable.settings.particleShader = "Shaders/GlowParticle.hlsl";
        editable.settings.postShader = "Shaders/PostEffects.hlsl";
        editable.objects[1].sky->ocean->solarIntensity = 0;
        editable.objects[1].sky->ocean->atmosphereSteps = 8;
        editable.objects[1].sky->ocean->solarSteps = 4;
        editable.objects[1].sky->ocean->cloudSteps = 12;
        editable.objects[1].sky->ocean->cloudLightSteps = 2;
        editable.settings.shadowResolution = 1024;
        editable.settings.localShadowResolution = 256;
        SceneEnvironment configured;
        Require(configured.Initialize(renderer, root, editable, error), error);
        configured.SeekAnimation(9, 9);
        Require(Capture(renderer, configured, "edited-quality", 640, 360) != baseline,
                "Saved ocean quality/lighting configuration did not affect GPU rendering");
        auto withModel = scene.World().Layout();
        const float azimuth = authored.objects[0].rotation[1];
        ScenePlacement model;
        model.id = "depth-fixture";
        model.name = "Depth fixture";
        model.meshRenderer.emplace();
        model.meshRenderer->model = "Assets/Models/Cube.obj";
        model.position = {std::sin(azimuth) * 16, -4, std::cos(azimuth) * 16};
        withModel.objects.push_back(model);
        SceneEnvironment below;
        Require(below.Initialize(renderer, root, withModel, error), error);
        below.SeekAnimation(9, 9);
        Require(Capture(renderer, below, "submerged-model", 640, 360) == baseline,
                "Water depth hides a fully submerged model");
        withModel.objects.back().position[1] = 4;
        SceneEnvironment above;
        Require(above.Initialize(renderer, root, withModel, error), error);
        above.SeekAnimation(9, 9);
        Require(Capture(renderer, above, "above-water-model", 640, 360) != baseline,
                "Above-water model composes with ray-rendered environment");
        auto legacy = authored;
        legacy.settings.postEffects.enabled = false;
        legacy.objects[1].sky->ocean.reset();
        legacy.objects[1].sky->horizon = {0, .2f, .8f};
        legacy.objects[1].sky->zenith = {0, .2f, .8f};
        legacy.objects[1].sky->sunStrength = 0;
        legacy.objects[1].sky->cloudOpacity = 0;
        SceneEnvironment old;
        Require(old.Initialize(renderer, root, legacy, error), error);
        const auto oldPixels = Capture(renderer, old, "legacy-sky", 64, 32);
        const auto center = (16 * 64 + 32) * 3;
        Require(oldPixels[center] == 0 && oldPixels[center + 1] == 51 && oldPixels[center + 2] == 204,
                "Legacy sky pixels are unchanged");
        Capture(renderer, scene, "portrait", 480, 800);
        auto sdrLayout = authored;
        sdrLayout.settings.postEffects.enabled = false;
        SceneEnvironment sdr;
        Require(sdr.Initialize(renderer, root, sdrLayout, error), error);
        const auto sdrPixels = Capture(renderer, sdr, "sdr", 640, 360);
        Require(std::any_of(sdrPixels.begin(), sdrPixels.end(),
                            [](auto value)
                            {
                                return value > 0;
                            }),
                "SDR environment renders without HDR effects");
        scene.PauseAudio(true);
        Require(scene.World().Layout().Serialize() == runtimeBaseline,
                "Playback does not mutate resolved runtime scene");
        Require(SceneLayout::Load(root / "Assets/Scenes/OceanSunset.json").Serialize() == authored.Serialize(),
                "Playback does not modify authored asset");
        Require(renderer.WaitForIdle(), "Final GPU idle");
        std::cout
            << "PASS: ocean scene, legacy serialization/pixels, parameter validation, D3D12/HDR, motion, editor camera, native model/water depth, portrait, wave-only audio\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        for (const auto& entry : Engine::Log::Recent())
            if (entry.level == Engine::LogLevel::Error)
                std::cerr << entry.text << '\n';
        return 1;
    }
}
