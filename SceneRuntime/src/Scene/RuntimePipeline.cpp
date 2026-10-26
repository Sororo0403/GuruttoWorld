#include <SceneRuntime/Scene/RuntimePipeline.h>

namespace
{
    struct PipelineFrame final : SceneRuntime::RuntimeSystem::Frame
    {
        std::vector<std::unique_ptr<SceneRuntime::RuntimeSystem::Frame>> frames;
        void BeforeFixed() override
        {
            for (const auto& frame : frames)
                frame->BeforeFixed();
        }
        bool Fixed(double seconds, const SceneRuntime::RuntimeInput& input,
                   const SceneRuntime::RuntimeScriptEffects& effects, std::string& error) override
        {
            for (const auto& frame : frames)
                if (!frame->Fixed(seconds, input, effects, error))
                    return false;
            return true;
        }
        bool Update(double seconds, const SceneRuntime::RuntimeScriptEffects& effects, std::string& error) override
        {
            for (const auto& frame : frames)
                if (!frame->Update(seconds, effects, error))
                    return false;
            return true;
        }
        bool BeforeUpdate(double seconds, const SceneRuntime::RuntimeInput& input, std::string& error) override
        {
            for (const auto& frame : frames)
                if (!frame->BeforeUpdate(seconds, input, error))
                    return false;
            return true;
        }
        bool Late(const SceneRuntime::RuntimeScriptEffects& effects, std::string& error) override
        {
            for (const auto& frame : frames)
                if (!frame->Late(effects, error))
                    return false;
            return true;
        }
        bool ReloadAssets(std::string& error) override
        {
            for (const auto& frame : frames)
                if (!frame->ReloadAssets(error))
                    return false;
            return true;
        }
        void SetInterpolation(double value) override
        {
            for (const auto& frame : frames)
                frame->SetInterpolation(value);
        }
        bool Prepare(const SceneRuntime::SceneChangeSet& changes, const SceneRuntime::RuntimeScriptEffects& effects,
                     std::string& error) override
        {
            auto combined = changes;
            for (const auto& frame : frames)
            {
                if (!frame->Prepare(combined, effects, error))
                    return false;
                combined.Merge(frame->Changes());
            }
            return true;
        }
        void Commit() noexcept override
        {
            for (const auto& frame : frames)
                frame->Commit();
        }
        void Finish() noexcept override
        {
            for (const auto& frame : frames)
                frame->Finish();
        }
        SceneRuntime::SceneChangeSet Changes() const override
        {
            SceneRuntime::SceneChangeSet changes;
            for (const auto& frame : frames)
                changes.Merge(frame->Changes());
            return changes;
        }
        std::vector<SceneRuntime::ScriptEvent> TakeEvents() override
        {
            std::vector<SceneRuntime::ScriptEvent> events;
            for (const auto& frame : frames)
                for (auto& event : frame->TakeEvents())
                    events.push_back(std::move(event));
            return events;
        }
    };
}
namespace SceneRuntime
{
    RuntimePipeline::RuntimePipeline(std::vector<std::unique_ptr<RuntimeSystem>> systems) : systems_(std::move(systems))
    {
        for (const auto& system : systems_)
            if (!system)
                throw std::invalid_argument("Missing runtime pipeline participant");
    }
    std::unique_ptr<RuntimeSystem::Frame> RuntimePipeline::Begin(WorldState::Transaction& transaction)
    {
        auto frame = std::make_unique<PipelineFrame>();
        for (const auto& system : systems_)
        {
            auto candidate = system->Begin(transaction);
            if (!candidate)
                throw std::runtime_error("Runtime participant did not create a candidate");
            frame->frames.push_back(std::move(candidate));
        }
        return frame;
    }
    RuntimeScriptQueries RuntimePipeline::Queries() const
    {
        std::vector<RuntimeScriptQueries> queries;
        for (const auto& system : systems_)
            queries.push_back(system->Queries());
        return {
            [queries = std::move(queries)](const std::array<float, 3>& origin, const std::array<float, 3>& direction,
                                           float distance, unsigned int mask, bool triggers, const std::string& ignore)
            {
                std::optional<PhysicsRayHit> closest;
                for (const auto& query : queries)
                    if (query.raycast)
                        if (auto hit = query.raycast(origin, direction, distance, mask, triggers, ignore);
                            hit && (!closest || hit->distance < closest->distance))
                            closest = std::move(hit);
                return closest;
            }};
    }
}
