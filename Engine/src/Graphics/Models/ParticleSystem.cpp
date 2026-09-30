#include <Engine/Graphics/Models/ParticleSystem.h>
#include <algorithm>
#include <cmath>

namespace Engine
{
    bool ParticleSystem::CreateGroup(const std::string& name, ID3D12Device* device, ID3D12CommandQueue* queue,
        std::shared_ptr<const Texture2D> texture, const std::filesystem::path& shaderPath)
    {
        if (name.empty() || groups_.contains(name)) return false;
        auto group = std::make_unique<Group>();
        if (!group->renderer.Initialize(device, queue, std::move(texture), shaderPath)) return false;
        group->particles.reserve(MaxParticlesPerGroup);
        groups_.emplace(name, std::move(group));
        return true;
    }
    bool ParticleSystem::Emit(const std::string& group, const Particle& particle)
    {
        const auto found = groups_.find(group);
        if (found == groups_.end() || found->second->particles.size() >= MaxParticlesPerGroup) return false;
        if (!std::all_of(particle.position.begin(), particle.position.end(),
            [](float value) { return std::isfinite(value); })) return false;
        if (!std::all_of(particle.velocity.begin(), particle.velocity.end(),
            [](float value) { return std::isfinite(value); })) return false;
        if (!std::all_of(particle.color.begin(), particle.color.end(),
            [](float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; })) return false;
        if (!std::isfinite(particle.size) || particle.size <= 0 || !std::isfinite(particle.lifetime) || particle.lifetime <= 0) return false;
        Particle spawned = particle;
        spawned.age = 0.0f;
        found->second->particles.push_back(spawned);
        return true;
    }
    void ParticleSystem::Update(double deltaSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0) return;
        for (auto& [name, group] : groups_)
        {
            for (auto& particle : group->particles)
            {
                const double age = particle.age + deltaSeconds;
                if (age >= particle.lifetime) { particle.age = particle.lifetime; continue; }
                particle.age = static_cast<float>(age);
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    particle.position[axis] = static_cast<float>(particle.position[axis] + particle.velocity[axis] * deltaSeconds);
                    if (!std::isfinite(particle.position[axis])) particle.age = particle.lifetime;
                }
            }
            std::erase_if(group->particles, [](const Particle& particle) { return particle.age >= particle.lifetime; });
        }
    }
    void ParticleSystem::Draw(ID3D12GraphicsCommandList* commands, const Camera& camera) const
    {
        using namespace DirectX;
        auto billboard = XMMatrixInverse(nullptr, camera.GetViewMatrix());
        billboard.r[3] = XMVectorSet(0, 0, 0, 1);
        const auto viewProjection = camera.GetViewMatrix() * camera.GetProjectionMatrix();
        for (const auto& [name, group] : groups_)
        {
            for (const auto& particle : group->particles)
            {
                XMFLOAT4X4 matrix;
                XMStoreFloat4x4(&matrix, XMMatrixScaling(particle.size, particle.size, particle.size) * billboard *
                    XMMatrixTranslation(particle.position[0], particle.position[1], particle.position[2]) * viewProjection);
                auto color = particle.color;
                color[3] *= 1.0f - particle.age / particle.lifetime;
                group->renderer.Draw(commands, matrix, color);
            }
        }
    }
    size_t ParticleSystem::GetParticleCount(const std::string& group) const
    {
        const auto found = groups_.find(group);
        return found == groups_.end() ? 0 : found->second->particles.size();
    }
}
