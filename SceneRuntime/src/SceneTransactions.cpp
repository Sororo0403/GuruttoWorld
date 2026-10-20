#include <SceneRuntime/SceneWorld.h>
#include <Engine/Core/Log.h>
#include <cmath>

namespace SceneRuntime
{
    bool SceneWorld::ReplaceRuntimeLayout(SceneLayout layout, const std::filesystem::path& assetsRoot,
                                          std::string& error)
    {
        try
        {
            if (!scripts_ || !ScriptRuntime::ValidateForPlay(layout, error))
                return false;
            PreparedLayout prepared;
            if (!PrepareLayout(std::move(layout), assetsRoot, error, true, prepared))
                return false;
            auto runtime = *scripts_;
            runtime.PrepareTransition(layout_, prepared.layout);
            auto started = prepared.layout;
            if (!runtime.StartCandidate(started, error))
                return false;
            if (!ScriptRuntime::ValidateForPlay(started, error))
                return false;
            PreparedLayout candidate;
            if (!PrepareScriptLayout(std::move(started), &prepared, candidate, error) ||
                (prepareRuntime_ && !prepareRuntime_(candidate.layout, error)) ||
                (prepareUi_ && !prepareUi_(candidate.layout, runtime.UiCommands(), error)))
                return false;
            if (!candidate.shadow)
                candidate.shadow = prepared.shadow;
            if (!candidate.localShadow)
                candidate.localShadow = prepared.localShadow;
            if (!FinalizeLifecycle(runtime, candidate, error))
                return false;
            CommitLayout(std::move(candidate), true);
            *scripts_ = std::move(runtime);
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
        catch (...)
        {
            error = "Scene transition preparation failed with an unknown exception";
            return false;
        }
    }
    bool SceneWorld::UpdateFrame(double seconds, float horizontal, float vertical, bool jump, bool advancePhysics)
    {
        if (!scripts_ || frameUpdating_ ||
            (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(horizontal) || !std::isfinite(vertical)))
            return false;
        auto baseline = RuntimeLayout();
        baseline.diagnostics = resourceDiagnostics_;
        auto runtime = *scripts_;
        auto physics = physics_;
        auto previous = previousPhysical_;
        auto current = currentPhysical_;
        const auto fixedSeconds = fixedSeconds_;
        const auto fixedJump = fixedJump_;
        auto pressed = fixedPressed_;
        const auto version = renderVersion_;
        auto transaction = physicsWorld_.BeginTransaction();
        frameUpdating_ = true;
        bool success = false;
        try
        {
            success =
                (!advancePhysics || MovePlayers(seconds, horizontal, vertical, jump)) && UpdateComponents(seconds);
        }
        catch (const std::exception& failure)
        {
            Engine::Log::Warning(failure.what());
        }
        catch (...)
        {
            Engine::Log::Warning("Frame preparation failed with an unknown exception");
        }
        frameUpdating_ = false;
        if (success)
        {
            transaction.Commit();
            return true;
        }
        CommitLayout(std::move(baseline), true);
        *scripts_ = std::move(runtime);
        physics_ = std::move(physics);
        previousPhysical_ = std::move(previous);
        currentPhysical_ = std::move(current);
        fixedSeconds_ = fixedSeconds;
        fixedJump_ = fixedJump;
        fixedPressed_ = std::move(pressed);
        renderVersion_ = version;
        preparedRenderVersion_ = static_cast<size_t>(-1);
        return false;
    }
    bool SceneWorld::FinalizeLifecycle(ScriptRuntime& runtime, PreparedLayout& prepared, std::string& error)
    {
        try
        {
            const bool stopped = runtime.HasPendingStops();
            auto layout = stopped ? prepared.layout : SceneLayout{};
            runtime.CommitStarts(stopped ? &layout : nullptr);
            if (!stopped || layout.Serialize() == prepared.layout.Serialize())
                return true;
            PreparedLayout candidate;
            if (!PrepareScriptLayout(std::move(layout), &prepared, candidate, error) ||
                (prepareRuntime_ && !prepareRuntime_(candidate.layout, error)) ||
                (prepareUi_ && !prepareUi_(candidate.layout, runtime.UiCommands(), error)))
                return false;
            if (!candidate.shadow)
                candidate.shadow = prepared.shadow;
            if (!candidate.localShadow)
                candidate.localShadow = prepared.localShadow;
            prepared = std::move(candidate);
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
}
