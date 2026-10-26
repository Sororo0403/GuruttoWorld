#include <SceneRuntime/Rendering/RenderPresentation.h>
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <Engine/Graphics/Resources/GpuProfiler.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    using namespace SceneRuntime;
    void DrawSky(ID3D12GraphicsCommandList* commands, const RenderPacket& layout,
                 const PresentationResources& resources, unsigned int width, unsigned int height, double seconds,
                 const Engine::Camera* camera)
    {
        if (layout.skies.empty())
            return;
        const auto& sky = layout.skies.front().settings;
        Engine::SpriteDrawParameters parameters;
        parameters.size = {static_cast<float>(width), static_cast<float>(height)};
        if (sky.ocean && camera &&
            (bool(resources.ocean) || resources.customOcean.contains(layout.settings.oceanShader)))
        {
            using namespace DirectX;
            const auto& ocean = *sky.ocean;
            XMFLOAT4X4 basis, projection;
            XMStoreFloat4x4(&basis, XMMatrixInverse(nullptr, camera->GetViewMatrix()));
            XMStoreFloat4x4(&projection, camera->GetProjectionMatrix());
            auto& values = parameters.pixelConstants;
            const auto put = [&](size_t offset, const std::array<float, 3>& rgb, float last)
            {
                std::copy(rgb.begin(), rgb.end(), values.begin() + offset);
                values[offset + 3] = last;
            };
            put(0, camera->GetPosition(), static_cast<float>(seconds));
            put(4, {basis._11, basis._12, basis._13}, 1.0f / projection._11);
            put(8, {basis._21, basis._22, basis._23}, 1.0f / projection._22);
            put(12, {basis._31, basis._32, basis._33}, ocean.seaLevel);
            constexpr float radians = std::numbers::pi_v<float> / 180.0f;
            const float elevation = ocean.sunElevation * radians, azimuth = ocean.sunAzimuth * radians;
            put(16,
                {std::sin(azimuth) * std::cos(elevation), std::sin(elevation), std::cos(azimuth) * std::cos(elevation)},
                ocean.sunDiameter * radians * 0.5f);
            put(20, {ocean.cloudBase, ocean.cloudThickness, ocean.cloudExtinction}, ocean.haze);
            put(24, {ocean.ozoneStrength, ocean.cloudAnisotropy, 0}, ocean.waveAmplitude);
            put(28, ocean.waterColor, ocean.wavePeriod);
            values[32] = ocean.cloudCoverage;
            values[33] = ocean.cloudSpeed;
            values[34] = ocean.refractiveIndex;
            values[35] = sky.cloudOpacity;
            values[36] = static_cast<float>(ocean.atmosphereSteps);
            values[37] = static_cast<float>(ocean.solarSteps);
            values[38] = static_cast<float>(ocean.cloudSteps);
            values[39] = static_cast<float>(ocean.cloudLightSteps);
            values[40] = static_cast<float>(ocean.waveCount);
            values[41] = static_cast<float>(ocean.seaSteps);
            values[42] = static_cast<float>(ocean.seaRefineSteps);
            values[43] = ocean.solarIntensity;
            put(44, {ocean.waveDirection, ocean.waveSpread, ocean.wavePeriodRatio}, ocean.waveAmplitudeRatio);
            put(48, {ocean.cloudScale, ocean.cloudWind[0], ocean.cloudWind[1]}, ocean.wavePhase);
            put(52, ocean.solarColor, ocean.traceDistance);
            put(56, ocean.cloudNoiseWeights, ocean.cloudDistance);
            put(60, ocean.cloudNoiseFrequencies, 0);
            put(64, ocean.cloudNoiseOffsets, 0);
            parameters.color = {projection._33, projection._43, 0, 1};
            if (layout.settings.oceanShader.empty())
                resources.ocean->Draw(commands, width, height, parameters);
            else if (const auto pipeline = resources.customOcean.find(layout.settings.oceanShader);
                     pipeline != resources.customOcean.end())
                pipeline->second->Draw(commands, width, height, parameters);
            return;
        }
        const float span = std::max(1.0f, sky.referenceAspect / (parameters.size[0] / parameters.size[1]));
        parameters.uvRect = {0, 0.5f - span * 0.5f, 1, 0.5f + span * 0.5f};
        auto& values = parameters.pixelConstants;
        const auto color = [&](size_t offset, const std::array<float, 3>& rgb, float last)
        {
            std::copy(rgb.begin(), rgb.end(), values.begin() + offset);
            values[offset + 3] = last;
        };
        color(0, sky.horizon, sky.horizonHeight);
        color(4, sky.zenith, sky.cloudOpacity);
        color(8, sky.sunColor, sky.sunStrength);
        values[12] = sky.sunCenter[0];
        values[13] = sky.sunCenter[1];
        values[14] = sky.sunRadius[0];
        values[15] = sky.sunRadius[1];
        color(16, sky.cloudLow, 0);
        color(20, sky.cloudHigh, 0);
        for (size_t index = 0; index < sky.clouds.size(); ++index)
        {
            const auto offset = 24 + index * 4;
            // 静止した雲は編集時の中心位置を正確に保持します。移動する雲は画面外で折り返します。
            for (size_t axis = 0; axis < 2; ++axis)
            {
                const auto center = sky.clouds[index].center[axis];
                const auto velocity = sky.cloudVelocity[axis];
                values[offset + axis] =
                    velocity == 0 ? center : static_cast<float>(center + std::remainder(seconds * velocity, 2.0));
            }
            values[offset + 2] = sky.clouds[index].size;
        }
        if (layout.settings.skyShader.empty() && resources.sky)
            resources.sky->Draw(commands, width, height, parameters);
        else if (const auto pipeline = resources.customSky.find(layout.settings.skyShader);
                 pipeline != resources.customSky.end())
            pipeline->second->Draw(commands, width, height, parameters);
    }
    void DrawParticles(ID3D12GraphicsCommandList* commands, const RenderPacket& packet,
                       const PresentationResources& resources, const Engine::Camera& camera, double seconds)
    {
        using namespace DirectX;
        auto billboard = XMMatrixInverse(nullptr, camera.GetViewMatrix());
        billboard.r[3] = XMVectorSet(0, 0, 0, 1);
        const auto viewProjection = camera.GetViewMatrix() * camera.GetProjectionMatrix();
        for (const auto& placement : packet.particles)
        {
            const auto& emitter = placement.settings;
            for (unsigned int index = 0; index < emitter.count; ++index)
            {
                const auto particle = RenderPresentation::Particle(placement, index, seconds);
                XMFLOAT4X4 matrix;
                XMStoreFloat4x4(&matrix, XMMatrixScaling(emitter.size, emitter.size, emitter.size) * billboard *
                                             XMMatrixTranslation(particle[0], particle[1], particle[2]) *
                                             viewProjection);
                const auto& shader = packet.settings.particleShader;
                if (shader.empty() && resources.particles)
                    resources.particles->Draw(commands, matrix,
                                              {emitter.color[0], emitter.color[1], emitter.color[2], particle[3]});
                else if (const auto found = resources.customParticles.find(shader);
                         found != resources.customParticles.end())
                    found->second->Draw(commands, matrix,
                                        {emitter.color[0], emitter.color[1], emitter.color[2], particle[3]});
            }
        }
    }
    bool BeginPostEffects(ID3D12GraphicsCommandList* commands, const RenderPacket& layout,
                          const PresentationResources& resources, unsigned int width, unsigned int height,
                          Engine::PostEffectRenderer*& post)
    {
        post = resources.postEffects.get();
        if (!layout.settings.postShader.empty())
        {
            const auto found = resources.customPost.find(layout.settings.postShader);
            if (found != resources.customPost.end())
                post = found->second.get();
        }
        bool processing = false;
        if (layout.settings.postEffects.enabled)
        {
            auto clear = layout.settings.background;
            for (size_t channel = 0; channel < 3; ++channel)
                clear[channel] = clear[channel] <= .04045f ? clear[channel] / 12.92f
                                                           : std::pow((clear[channel] + .055f) / 1.055f, 2.4f);
            processing = post && post->Ready() && post->Begin(commands, width, height, clear);
            if (!processing)
                Engine::Log::Warning("Post effect target could not be prepared");
        }
        return processing;
    }
    bool DrawOverlay(ID3D12GraphicsCommandList* commands, const PresentationPacket& frame, unsigned int width,
                     unsigned int height, const SceneDrawOptions& options)
    {
        const auto& [sceneCamera, seconds, motionEnabled, uiState, showSceneUi, showGameUi] = options;
        if (!frame.scene->ui)
            return true;
        if (sceneCamera)
        {
            Engine::GpuScope gpu(commands, "Scene UI");
            Engine::CpuScope cpu("Scene UI");
            if (showSceneUi)
                return UiRendering::DrawScene(commands, *frame.scene->ui, frame.ui, *sceneCamera,
                                              uiState ? *uiState : UiState{});
        }
        else if (showGameUi)
        {
            Engine::GpuScope gpu(commands, "Game UI");
            Engine::CpuScope cpu("Game UI");
            return UiRendering::Draw(commands, *frame.scene->ui, frame.ui, width, height,
                                     uiState ? *uiState : UiState{});
        }
        return true;
    }
}
namespace SceneRuntime
{
    std::array<float, 4> RenderPresentation::Particle(const RenderNode<ParticleEmitterComponent>& placement,
                                                      unsigned int index, double seconds)
    {
        if (index >= placement.settings.count || !std::isfinite(seconds))
            return {};
        const auto& emitter = placement.settings;
        const float phase = static_cast<float>(std::fmod(seconds / emitter.cycle + double(index) / emitter.count, 1.0));
        const float wave = std::sin(phase * std::numbers::pi_v<float>);
        const float x = float((index * 7) % emitter.count) / emitter.count;
        const float z = float((index * 11) % emitter.count) / emitter.count;
        const std::array<float, 3> local{x * emitter.extent[0] + phase * emitter.travel[0] + emitter.drift * wave,
                                         float((index * 13) % emitter.count) / emitter.count * emitter.extent[1] +
                                             phase * emitter.travel[1],
                                         z * emitter.extent[2] + phase * emitter.travel[2]};
        const auto& transform = placement.matrix;
        DirectX::XMFLOAT3 position;
        DirectX::XMStoreFloat3(&position,
                               DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(local[0], local[1], local[2], 1),
                                                                DirectX::XMLoadFloat4x4(&transform)));
        return {position.x, position.y, position.z, emitter.color[3] * wave * wave};
    }
    bool RenderPresentation::Draw(const PresentationPacket& frame, ID3D12GraphicsCommandList* commands,
                                  unsigned int width, unsigned int height, const SceneDrawOptions& options,
                                  Engine::OcclusionRenderer& occlusion, MeshTelemetry& telemetry)
    {
        const auto& [sceneCamera, seconds, motionEnabled, uiState, showSceneUi, showGameUi] = options;
        telemetry = {};
        if (!width || !height)
            return true;
        if (!frame.scene || !frame.resources || !commands)
            return false;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(commands->GetDevice(IID_PPV_ARGS(&device))) || frame.resources->device != device ||
            frame.scene->device != reinterpret_cast<std::uintptr_t>(device.Get()))
            return false;
        if (!Engine::RenderFrameContext::Retain(commands, frame.resources))
            return false;
        const auto& packet = *frame.scene;
        const auto& resources = *frame.resources;
        Engine::PostEffectRenderer* post = nullptr;
        const bool processing = BeginPostEffects(commands, packet, resources, width, height, post);
        struct PendingPost
        {
            Engine::PostEffectRenderer* post;
            ID3D12GraphicsCommandList* commands;
            ~PendingPost()
            {
                if (post)
                    post->Cancel(commands);
            }
        } pending{processing ? post : nullptr, commands};
        if (packet.settings.postEffects.enabled && !processing)
            return false;
        Engine::Camera gameCamera;
        const bool hasCamera = sceneCamera || SceneView::Camera(packet, float(width) / height, seconds, gameCamera);
        const auto* activeCamera = sceneCamera ? sceneCamera : hasCamera ? &gameCamera : nullptr;
        {
            Engine::GpuScope gpu(commands, "Sky");
            Engine::CpuScope cpu("Sky");
            DrawSky(commands, packet, resources, width, height, seconds, activeCamera);
        }
        if (hasCamera)
        {
            const auto& camera = sceneCamera ? *sceneCamera : gameCamera;
            const auto defaults = uiState || !packet.ui ? UiState{} : UiGeometry::Defaults(*packet.ui);
            if (!RenderScene::Draw(frame.scene, commands, camera, SceneView::Light(packet),
                                   uiState ? *uiState : defaults, occlusion, telemetry))
                return false;
            if (motionEnabled)
            {
                Engine::GpuScope gpu(commands, "Particles");
                Engine::CpuScope cpu("Particles");
                DrawParticles(commands, packet, resources, camera, seconds);
            }
        }
        if (processing)
        {
            Engine::GpuScope gpu(commands, "Post effects");
            Engine::CpuScope cpu("Post effects");
            if (!post->End(commands, packet.settings.postEffects))
                return false;
            pending.post = nullptr;
        }
        return DrawOverlay(commands, frame, width, height, options);
    }
}
