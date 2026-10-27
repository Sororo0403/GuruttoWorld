#include <SceneRuntime/Rendering/SceneRenderState.h>
#include <SceneRuntime/Rendering/ScenePresentation.h>
#include <SceneRuntime/Scene/SceneWorld.h>
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
        sky_ = std::make_shared<Engine::SpriteRenderer>();
        ocean_ = std::make_shared<Engine::SpriteRenderer>();
        particles_ = std::make_shared<Engine::ParticleRenderer>();
        postEffects_ = std::make_shared<Engine::PostEffectRenderer>();
        customSky_.clear();
        customOcean_.clear();
        customParticles_.clear();
        customPost_.clear();
        skyReady_ = particlesReady_ = oceanReady_ = false;
        resources_.reset();
        environmentTexture_.reset();
        ui_ = SceneUi{};
        root_ = context_->Root();
        device_ = renderer.GetDevice();
        postShader_ = context_->Resolve(root_ / "Engine/Shaders/PostEffects.hlsl");
        queue_ = renderer.GetCommandQueue();
        oceanShader_ = context_->Resolve(root_ / "Engine/Shaders/OceanSunset.hlsl");
        error.clear();
        return true;
    }

    bool ScenePresentation::PrepareDefaultSprites(const SceneLayout& layout, bool sky, bool particles,
                                                  std::string& error) const
    {
        if (sky && layout.settings.skyShader.empty() && !skyReady_)
        {
            if (!context_->CheckConfiguration(error))
                return false;
            if (!sky_->Initialize(device_.Get(), queue_.Get(), environmentTexture_,
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
            if (!particles_->Initialize(device_.Get(), queue_.Get(), environmentTexture_,
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
            if (!ocean_->Initialize(device_.Get(), queue_.Get(), environmentTexture_, oceanShader_, true, true))
            {
                error = "Ocean sunset pipeline could not be initialized";
                return false;
            }
            oceanReady_ = true;
        }
        if (!layout.settings.postEffects.enabled || !layout.settings.postShader.empty() || postEffects_->Ready())
            return true;
        if (!context_->CheckConfiguration(error))
            return false;
        if (!postEffects_->Initialize(device_.Get(), postShader_))
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
        if (!PrepareDefaultEffects(layout, ocean, error))
            return false;
        RefreshResources();
        return true;
    }

    void ScenePresentation::RefreshResources() const
    {
        const auto sky = skyReady_ ? sky_ : nullptr, ocean = oceanReady_ ? ocean_ : nullptr;
        const auto particles = particlesReady_ ? particles_ : nullptr;
        const auto post = postEffects_ && postEffects_->Ready() ? postEffects_ : nullptr;
        if (resources_ && resources_->device == device_ && resources_->sky == sky && resources_->ocean == ocean &&
            resources_->particles == particles && resources_->postEffects == post &&
            resources_->customSky == customSky_ && resources_->customOcean == customOcean_ &&
            resources_->customParticles == customParticles_ && resources_->customPost == customPost_)
            return;
        auto resources = std::make_shared<PresentationResources>();
        resources->device = device_;
        resources->sky = sky;
        resources->ocean = ocean;
        resources->particles = particles;
        resources->postEffects = post;
        resources->customSky = customSky_;
        resources->customOcean = customOcean_;
        resources->customParticles = customParticles_;
        resources->customPost = customPost_;
        resources_ = std::move(resources);
    }
    PresentationPacket ScenePresentation::Capture(std::shared_ptr<const RenderPacket> packet) const noexcept
    {
        return {std::move(packet), resources_, ui_.RenderResources()};
    }
    void ScenePresentation::Draw(ID3D12GraphicsCommandList* commands, const SceneWorld& world, unsigned int width,
                                 unsigned int height, const SceneDrawOptions& options) const
    {
        if (!world.renderState_->DrawPresentation(Capture(world.RenderSnapshot()), commands, width, height, options))
            throw std::runtime_error("Presentation packet could not be recorded");
    }

}
