#include <SceneRuntime/Scene/SceneWorld.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/Profiler.h>
#include <cmath>

namespace
{
    bool ValidUpdateInput(double seconds, float horizontal, float vertical)
    {
        return std::isfinite(seconds) && seconds > 0 && std::isfinite(horizontal) && std::isfinite(vertical);
    }
}

namespace SceneRuntime
{
    bool SceneWorld::CanMutateScene(std::string* error) const
    {
        if (!execution_ || !execution_->values.frameUpdating)
            return true;
        if (error)
            *error = "Scene mutation is unavailable during frame preparation";
        return false;
    }
    void SceneWorld::ForgetExecutionObject(const std::string& id)
    {
        if (!execution_)
            return;
        execution_->values.physics.erase(id);
        execution_->values.previousPhysical.erase(id);
        execution_->values.currentPhysical.erase(id);
    }

    bool SceneWorld::PrepareRuntimeCallbacks(const SceneLayout& layout, const ScriptRuntime& runtime,
                                             std::string& error) const
    {
        return (!prepareRuntime_ || prepareRuntime_(layout, error)) &&
               (!prepareUi_ || prepareUi_(layout, runtime.UiCommands(), error));
    }
    bool SceneWorld::ReplaceRuntimeLayout(SceneLayout layout, const std::filesystem::path& assetsRoot,
                                          std::string& error)
    {
        if (!CanMutateScene(&error))
            return false;
        try
        {
            if (!execution_ ||
                !ScriptRuntime::ValidateForPlay(layout, error, &execution_->values.scripts.Definitions()))
                return false;
            PreparedLayout prepared;
            if (!PrepareLayout(std::move(layout), assetsRoot, error, true, prepared))
                return false;
            auto runtime = execution_->values.scripts;
            runtime.PrepareTransition(layout_, prepared.layout);
            auto started = prepared.layout;
            if (!runtime.StartCandidate(started, error))
                return false;
            if (!ScriptRuntime::ValidateForPlay(started, error, &execution_->values.scripts.Definitions()))
                return false;
            PreparedLayout candidate;
            if (!PrepareScriptLayout(std::move(started), &prepared, candidate, error) ||
                !PrepareRuntimeCallbacks(candidate.layout, runtime, error))
                return false;
            if (!candidate.shadow)
                candidate.shadow = prepared.shadow;
            if (!candidate.localShadow)
                candidate.localShadow = prepared.localShadow;
            if (!ScriptRuntime::ValidateForPlay(candidate.layout, error, &execution_->values.scripts.Definitions()))
                return false;
            CommitLayout(std::move(candidate), true);
            runtime.CommitStarts();
            execution_->values.scripts = std::move(runtime);
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
    struct SceneWorld::FrameTransaction final
    {
        SceneWorld& world;
        SceneExecutionState::Values saved;
        PhysicsWorld::Transaction physics;
        bool committed = false;

        // 未確定の実行値だけを分離し、配置と描画資源は確定まで現状を維持します。
        explicit FrameTransaction(SceneWorld& target)
            : world(target), saved(target.execution_->values),
              physics(target.execution_->physicsWorld.BeginTransaction())
        {
            world.execution_->values.frameUpdating = true;
        }
        // 配置の中間確定がないため、実行値と物理だけを同じ保存点へ戻します。
        ~FrameTransaction()
        {
            if (!committed)
            {
                world.execution_->values = std::move(saved);
                world.preparedRenderVersion_ = static_cast<size_t>(-1);
            }
            world.execution_->values.frameUpdating = false;
        }
        void Commit() noexcept
        {
            physics.Commit();
            world.execution_->values.scripts.CommitStarts();
            committed = true;
        }
        FrameTransaction(const FrameTransaction&) = delete;
        FrameTransaction& operator=(const FrameTransaction&) = delete;
    };

    bool SceneWorld::UpdateFrame(double seconds, float horizontal, float vertical, bool jump, bool advancePhysics)
    {
        return ExecuteUpdate(seconds, horizontal, vertical, jump, advancePhysics, true);
    }
    bool SceneWorld::ExecuteUpdate(double seconds, float horizontal, float vertical, bool jump, bool movement,
                                   bool components)
    {
        if (!execution_ || execution_->values.frameUpdating || !ValidUpdateInput(seconds, horizontal, vertical))
            return false;
        try
        {
            FrameTransaction transaction = [&]
            {
                Engine::CpuScope timing("Frame transaction snapshot");
                return FrameTransaction(*this);
            }();
            // 固定更新と可変更新が同じ候補を所有し、成功時に一度だけ投影します。
            auto candidate = [&]
            {
                Engine::CpuScope timing("Frame layout copy");
                return layout_;
            }();
            if (movement && !PrepareMovement(candidate, seconds, horizontal, vertical, jump))
                return false;
            PreparedLayout prepared;
            std::vector<AnimatorFrame> frames;
            std::map<std::string, std::array<float, 3>> impulses;
            std::string error;
            const bool ready = components
                                   ? PrepareComponents(std::move(candidate), seconds, prepared, frames, impulses, error)
                                   : PrepareScriptLayout(std::move(candidate), nullptr, prepared, error) &&
                                         PrepareRuntimeCallbacks(prepared.layout, execution_->values.scripts, error) &&
                                         ValidateScriptsCandidate(prepared.layout, error);
            if (!ready)
            {
                Engine::Log::Warning(error);
                return false;
            }
            {
                Engine::CpuScope timing("Frame commit");
                CommitLayout(std::move(prepared), true);
                CommitAnimatorFrames(frames);
                for (const auto& [id, impulse] : impulses)
                    execution_->physicsWorld.AddImpulse(id, impulse);
                transaction.Commit();
            }
            return true;
        }
        catch (const std::exception& failure)
        {
            Engine::Log::Warning(failure.what());
        }
        catch (...)
        {
            Engine::Log::Warning("Frame preparation failed with an unknown exception");
        }
        return false;
    }
    bool SceneWorld::ValidateScriptsCandidate(const SceneLayout& candidate, std::string& error) const
    {
        return ScriptRuntime::ValidateForPlay(candidate, error, &execution_->values.scripts.Definitions());
    }
}
