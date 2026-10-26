#include <SceneRuntime/Rendering/ScenePresentation.h>
#include <Engine/Graphics/Resources/GpuProfiler.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <Engine/Core/Log.h>

namespace
{
    template <class Cache, class Initialize>
    bool PrepareRenderer(const std::filesystem::path& path, Cache& cache, Initialize initialize, std::string& error,
                         const char* message)
    {
        if (path.empty() || cache.contains(path))
            return true;
        auto renderer = std::make_shared<typename Cache::mapped_type::element_type>();
        error.clear();
        if (!initialize(*renderer, error))
        {
            if (error.empty())
                error = message;
            return false;
        }
        cache.emplace(path, std::move(renderer));
        return true;
    }
}

namespace SceneRuntime
{
    bool ScenePresentation::Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                                       std::string& error, std::shared_ptr<const Engine::ContentContext> context)
    {
        context_ = context ? std::move(context) : Engine::ContentContext::Capture(root);
        context_->RequireRoot(root);
        if (!context_->CheckConfiguration(error))
            return false;
        root_ = context_->Root();
        device_ = renderer.GetDevice();
        postShader_ = context_->Resolve(root_ / "Engine/Shaders/PostEffects.hlsl");
        queue_ = renderer.GetCommandQueue();
        oceanShader_ = context_->Resolve(root_ / "Engine/Shaders/OceanSunset.hlsl");
        error.clear();
        return true;
    }
    void ScenePresentation::DrawSky(ID3D12GraphicsCommandList* commands, const SceneLayout& layout, unsigned int width,
                                    unsigned int height, double seconds, const Engine::Camera* camera) const
    {
        const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                        [](const auto& object)
                                        {
                                            return object.sky && object.sky->enabled;
                                        });
        if (found == layout.objects.end())
            return;
        const auto& sky = *found->sky;
        Engine::SpriteDrawParameters parameters;
        parameters.size = {static_cast<float>(width), static_cast<float>(height)};
        if (sky.ocean && camera && (oceanReady_ || customOcean_.contains(layout.settings.oceanShader)))
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
                ocean_.Draw(commands, width, height, parameters);
            else if (const auto pipeline = customOcean_.find(layout.settings.oceanShader);
                     pipeline != customOcean_.end())
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
        if (layout.settings.skyShader.empty())
            sky_.Draw(commands, width, height, parameters);
        else if (const auto pipeline = customSky_.find(layout.settings.skyShader); pipeline != customSky_.end())
            pipeline->second->Draw(commands, width, height, parameters);
    }
    std::array<float, 4> ScenePresentation::Particle(const SceneWorld& world, const ScenePlacement& placement,
                                                     unsigned int index, double seconds)
    {
        if (!placement.particleEmitter || index >= placement.particleEmitter->count || !std::isfinite(seconds))
            return {};
        const auto& emitter = *placement.particleEmitter;
        const float phase = static_cast<float>(std::fmod(seconds / emitter.cycle + double(index) / emitter.count, 1.0));
        const float wave = std::sin(phase * std::numbers::pi_v<float>);
        const float x = float((index * 7) % emitter.count) / emitter.count;
        const float z = float((index * 11) % emitter.count) / emitter.count;
        const std::array<float, 3> local{x * emitter.extent[0] + phase * emitter.travel[0] + emitter.drift * wave,
                                         float((index * 13) % emitter.count) / emitter.count * emitter.extent[1] +
                                             phase * emitter.travel[1],
                                         z * emitter.extent[2] + phase * emitter.travel[2]};
        DirectX::XMFLOAT4X4 transform;
        if (!world.WorldMatrix(placement.id, transform))
            return {};
        DirectX::XMFLOAT3 position;
        DirectX::XMStoreFloat3(&position,
                               DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(local[0], local[1], local[2], 1),
                                                                DirectX::XMLoadFloat4x4(&transform)));
        return {position.x, position.y, position.z, emitter.color[3] * wave * wave};
    }
    void ScenePresentation::DrawParticles(ID3D12GraphicsCommandList* commands, const SceneWorld& world,
                                          const Engine::Camera& camera, double seconds) const
    {
        using namespace DirectX;
        auto billboard = XMMatrixInverse(nullptr, camera.GetViewMatrix());
        billboard.r[3] = XMVectorSet(0, 0, 0, 1);
        const auto viewProjection = camera.GetViewMatrix() * camera.GetProjectionMatrix();
        for (const auto& placement : world.Layout().objects)
        {
            if (!placement.particleEmitter || !placement.particleEmitter->enabled)
                continue;
            const auto& emitter = *placement.particleEmitter;
            for (unsigned int index = 0; index < emitter.count; ++index)
            {
                const auto particle = Particle(world, placement, index, seconds);
                XMFLOAT4X4 matrix;
                XMStoreFloat4x4(&matrix, XMMatrixScaling(emitter.size, emitter.size, emitter.size) * billboard *
                                             XMMatrixTranslation(particle[0], particle[1], particle[2]) *
                                             viewProjection);
                const auto& shader = world.Layout().settings.particleShader;
                if (shader.empty())
                    particles_.Draw(commands, matrix,
                                    {emitter.color[0], emitter.color[1], emitter.color[2], particle[3]});
                else if (const auto found = customParticles_.find(shader); found != customParticles_.end())
                    found->second->Draw(commands, matrix,
                                        {emitter.color[0], emitter.color[1], emitter.color[2], particle[3]});
            }
        }
    }
    bool ScenePresentation::PrepareDefaultSprites(const SceneLayout& layout, bool sky, bool particles,
                                                  std::string& error) const
    {
        if (sky && layout.settings.skyShader.empty() && !skyReady_)
        {
            if (!context_->CheckConfiguration(error))
                return false;
            if (!sky_.Initialize(device_.Get(), queue_.Get(), environmentTexture_,
                                 context_->Resolve(root_ / "Engine/Shaders/Sky.hlsl")))
            {
                error = "Sky pipeline could not be initialized";
                return false;
            }
            skyReady_ = true;
        }
        if (particles && layout.settings.particleShader.empty() && !particlesReady_)
        {
            if (!context_->CheckConfiguration(error))
                return false;
            if (!particles_.Initialize(device_.Get(), queue_.Get(), environmentTexture_,
                                       context_->Resolve(root_ / "Engine/Shaders/GlowParticle.hlsl")))
            {
                error = "Particle pipeline could not be initialized";
                return false;
            }
            particlesReady_ = true;
        }
        return true;
    }
    bool ScenePresentation::PrepareCustomSprites(const SceneLayout& layout, bool sky, bool ocean,
                                                 std::string& error) const
    {
        const auto sprite = [&](const auto& path, auto& cache, bool depth)
        {
            return PrepareRenderer(
                path, cache,
                [&](auto& renderer, std::string& error)
                {
                    if (!context_->CheckConfiguration(error))
                        return false;
                    return renderer.Initialize(device_.Get(), queue_.Get(), environmentTexture_,
                                               context_->Resolve(root_ / path), depth, depth);
                },
                error, "Scene sprite shader could not be initialized");
        };
        if ((sky && !sprite(layout.settings.skyShader, customSky_, false)) ||
            (ocean && !sprite(layout.settings.oceanShader, customOcean_, true)))
            return false;
        return true;
    }
    bool ScenePresentation::PrepareCustomEffects(const SceneLayout& layout, bool particles, std::string& error) const
    {
        if (particles && !PrepareRenderer(
                             layout.settings.particleShader, customParticles_,
                             [&](auto& renderer, std::string& error)
                             {
                                 if (!context_->CheckConfiguration(error))
                                     return false;
                                 return renderer.Initialize(device_.Get(), queue_.Get(), environmentTexture_,
                                                            context_->Resolve(root_ / layout.settings.particleShader));
                             },
                             error, "Scene particle shader could not be initialized"))
            return false;
        if (layout.settings.postEffects.enabled &&
            !PrepareRenderer(
                layout.settings.postShader, customPost_,
                [&](auto& renderer, std::string& error)
                {
                    if (!context_->CheckConfiguration(error))
                        return false;
                    return renderer.Initialize(device_.Get(), context_->Resolve(root_ / layout.settings.postShader));
                },
                error, "Scene post effect shader could not be initialized"))
            return false;
        return true;
    }
    bool ScenePresentation::PrepareDefaultEffects(const SceneLayout& layout, bool ocean, std::string& error) const
    {
        if (ocean && layout.settings.oceanShader.empty() && !oceanReady_)
        {
            if (!context_->CheckConfiguration(error))
                return false;
            if (!ocean_.Initialize(device_.Get(), queue_.Get(), environmentTexture_, oceanShader_, true, true))
            {
                error = "Ocean sunset pipeline could not be initialized";
                return false;
            }
            oceanReady_ = true;
        }
        if (!layout.settings.postEffects.enabled || !layout.settings.postShader.empty() || postEffects_.Ready())
            return true;
        if (!context_->CheckConfiguration(error))
            return false;
        if (!postEffects_.Initialize(device_.Get(), postShader_))
        {
            error = "Post effect pipeline could not be initialized";
            return false;
        }
        return true;
    }
    bool ScenePresentation::PrepareEffects(const SceneLayout& layout, std::string& error) const
    {
        const bool sky = std::any_of(layout.objects.begin(), layout.objects.end(),
                                     [](const auto& object)
                                     {
                                         return object.sky && object.sky->enabled;
                                     });
        const bool particles = std::any_of(layout.objects.begin(), layout.objects.end(),
                                           [](const auto& object)
                                           {
                                               return object.particleEmitter && object.particleEmitter->enabled;
                                           });
        if ((sky || particles) && !environmentTexture_)
        {
            auto texture = std::make_shared<Engine::Texture2D>();
            if (!texture->Initialize(device_.Get(), queue_.Get(), {}))
            {
                error = "Cannot prepare effect texture";
                return false;
            }
            environmentTexture_ = std::move(texture);
        }
        if (!PrepareDefaultSprites(layout, sky, particles, error))
            return false;
        const bool ocean = std::any_of(layout.objects.begin(), layout.objects.end(),
                                       [](const auto& object)
                                       {
                                           return object.sky && object.sky->enabled && object.sky->ocean.has_value();
                                       });
        if (!PrepareCustomSprites(layout, sky, ocean, error))
            return false;
        if (!PrepareCustomEffects(layout, particles, error))
            return false;
        return PrepareDefaultEffects(layout, ocean, error);
    }
    bool ScenePresentation::BeginPostEffects(ID3D12GraphicsCommandList* commands, const SceneLayout& layout,
                                             unsigned int width, unsigned int height,
                                             Engine::PostEffectRenderer*& post) const
    {
        post = &postEffects_;
        if (!layout.settings.postShader.empty())
        {
            const auto found = customPost_.find(layout.settings.postShader);
            if (found != customPost_.end())
                post = found->second.get();
        }
        bool processing = false;
        if (layout.settings.postEffects.enabled)
        {
            auto clear = layout.settings.background;
            for (size_t channel = 0; channel < 3; ++channel)
                clear[channel] = clear[channel] <= .04045f ? clear[channel] / 12.92f
                                                           : std::pow((clear[channel] + .055f) / 1.055f, 2.4f);
            processing = post->Ready() && post->Begin(commands, width, height, clear);
            if (!processing)
                Engine::Log::Warning("Post effect target could not be prepared");
        }
        return processing;
    }
    void ScenePresentation::DrawOverlay(ID3D12GraphicsCommandList* commands, const SceneWorld& world,
                                        unsigned int width, unsigned int height, const SceneDrawOptions& options) const
    {
        const auto& [sceneCamera, seconds, motionEnabled, uiState, showSceneUi, showGameUi] = options;
        if (sceneCamera)
        {
            Engine::GpuScope gpu(commands, "Scene UI");
            Engine::CpuScope cpu("Scene UI");
            if (showSceneUi)
                ui_.DrawScene(commands, world, *sceneCamera, uiState ? *uiState : UiState{});
        }
        else if (showGameUi)
        {
            Engine::GpuScope gpu(commands, "Game UI");
            Engine::CpuScope cpu("Game UI");
            DrawUi(commands, world.Layout(), width, height, uiState ? *uiState : UiState{});
        }
    }
    void ScenePresentation::Draw(ID3D12GraphicsCommandList* commands, const SceneWorld& world, unsigned int width,
                                 unsigned int height, const SceneDrawOptions& options) const
    {
        const auto& [sceneCamera, seconds, motionEnabled, uiState, showSceneUi, showGameUi] = options;
        if (!width || !height)
            return;
        Engine::PostEffectRenderer* post = nullptr;
        const bool processing = BeginPostEffects(commands, world.Layout(), width, height, post);
        Engine::Camera gameCamera;
        const bool hasCamera = sceneCamera || SceneView::Camera(world, float(width) / height, seconds, gameCamera);
        const auto* activeCamera = sceneCamera ? sceneCamera : hasCamera ? &gameCamera : nullptr;
        {
            Engine::GpuScope gpu(commands, "Sky");
            Engine::CpuScope cpu("Sky");
            DrawSky(commands, world.Layout(), width, height, seconds, activeCamera);
        }
        if (hasCamera)
        {
            const auto& camera = sceneCamera ? *sceneCamera : gameCamera;
            world.Draw(commands, camera, SceneView::Light(world), uiState);
            if (motionEnabled)
            {
                Engine::GpuScope gpu(commands, "Particles");
                Engine::CpuScope cpu("Particles");
                DrawParticles(commands, world, camera, seconds);
            }
        }
        if (processing)
        {
            Engine::GpuScope gpu(commands, "Post effects");
            Engine::CpuScope cpu("Post effects");
            if (!post->End(commands, world.Layout().settings.postEffects))
                Engine::Log::Warning("Post effect rendering failed");
        }
        DrawOverlay(commands, world, width, height, options);
    }
}
