#include <SceneRuntime/Scene/RuntimeSession.h>
#include <Engine/Core/Profiler.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    double StepSeconds(float value)
    {
        return value == 1.0f / 60.0f ? 1.0 / 60.0 : static_cast<double>(value);
    }
}

namespace SceneRuntime
{
    struct RuntimeSession::Impl
    {
        WorldState world;
        RuntimeScripts scripts;
        std::unique_ptr<RuntimeSystem> system;
        SceneChangeSet changes;
        double fixedSeconds = 0;
        std::uint64_t fixedTicks = 0;
        std::map<std::string, bool> pressed;
        bool jump = false, active = false, started = false;
        Impl(std::shared_ptr<const WorldDefinition> definition,
             std::shared_ptr<const RuntimeScriptDefinitions> definitions)
            : world(std::move(definition)), scripts(std::move(definitions))
        {
        }
    };
    RuntimeSession::RuntimeSession(std::shared_ptr<const WorldDefinition> definition,
                                   std::shared_ptr<const RuntimeScriptDefinitions> scripts)
        : impl_(std::make_unique<Impl>(std::move(definition), std::move(scripts)))
    {
    }
    RuntimeSession::~RuntimeSession()
    {
        impl_->scripts.Stop(impl_->world);
    }
    WorldState::ReadView RuntimeSession::Read() const noexcept
    {
        return impl_->world.Read();
    }
    bool RuntimeSession::Start(std::string& error)
    {
        return Execute(0, {}, false, nullptr, {}, true, error);
    }
    bool RuntimeSession::Tick(double seconds, const RuntimeInput& input, bool advancePhysics, std::string& error)
    {
        if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(input.horizontal) ||
            !std::isfinite(input.vertical))
        {
            error = "Invalid runtime tick input";
            return false;
        }
        for (const auto& [name, value] : input.values)
            if (name.empty() || !std::isfinite(value))
            {
                error = "Invalid runtime action input";
                return false;
            }
        return Execute(seconds, input, advancePhysics, nullptr, {}, false, error);
    }
    bool RuntimeSession::Edit(const SceneEdit& edit, std::string& error, bool start)
    {
        return Execute(0, {}, false, &edit, {}, true, error, true, nullptr, start);
    }
    bool RuntimeSession::Pause(bool paused, std::string& error)
    {
        return Execute(0, {}, false, nullptr, paused, true, error);
    }
    bool RuntimeSession::Execute(double seconds, const RuntimeInput& input, bool advancePhysics, const SceneEdit* edit,
                                 std::optional<bool> paused, bool startOnly, std::string& error, bool components,
                                 const RuntimeScriptEffects* effects, bool startAfterEdit,
                                 const std::map<std::string, float>* clocks)
    {
        if (impl_->active)
        {
            error = "Reentrant runtime session update";
            return false;
        }
        struct Active
        {
            bool& flag;
            explicit Active(bool& value) : flag(value)
            {
                flag = true;
            }
            ~Active()
            {
                flag = false;
            }
        } active(impl_->active);
        try
        {
            Engine::CpuScope scope("Runtime session tick");
            auto transaction = impl_->world.BeginTransaction();
            auto frame = impl_->scripts.BeginFrame(transaction, input.values, input.pressed,
                                                   impl_->system ? impl_->system->Queries() : RuntimeScriptQueries{});
            if (edit && !frame.Edit(*edit, error))
                return false;
            if (effects && !frame.ApplyEffects(*effects, error))
                return false;
            bool animated = false;
            if (clocks)
            {
                if (!frame.Animate(*clocks, error, animated))
                    return false;
                if (!animated)
                {
                    error.clear();
                    return true;
                }
            }
            if (!impl_->scripts.Validate(transaction.Read(), error))
                return false;
            auto system = impl_->system ? impl_->system->Begin(transaction) : nullptr;
            if (impl_->system && !system)
                throw std::runtime_error("Runtime system did not create a candidate");
            const bool starting = (!edit && !paused && !effects && components) || startAfterEdit;
            auto remaining = impl_->fixedSeconds;
            auto pressed = impl_->pressed;
            auto jump = impl_->jump;
            auto ticks = impl_->fixedTicks;
            const auto settingsDefinition = transaction.Read().Definition();
            const auto& settings = settingsDefinition->Source().settings;
            const auto fixedStep = StepSeconds(settings.fixedStep);
            if (fixedStep != StepSeconds(impl_->world.Read().Definition()->Source().settings.fixedStep))
            {
                remaining = 0;
                pressed.clear();
                jump = false;
            }
            bool latePhase = false;
            if (startOnly)
            {
                if (system && !system->BeforeUpdate(0, input, error))
                    return false;
                if (paused)
                {
                    if (!frame.Pause(*paused, error))
                        return false;
                }
                else if ((starting || (edit && impl_->started)) && !frame.Start(error))
                    return false;
            }
            else
            {
                if (advancePhysics)
                {
                    remaining += std::min(seconds, .1);
                    for (const auto& [name, value] : input.pressed)
                        if (value)
                            pressed[name] = true;
                    jump |= input.jump;
                    unsigned int steps = 0;
                    while (remaining + 1e-10 >= fixedStep && steps < settings.maximumFixedSteps)
                    {
                        if (ticks == std::numeric_limits<std::uint64_t>::max())
                            throw std::runtime_error("Runtime fixed clock exhausted");
                        if (system)
                            system->BeforeFixed();
                        frame.SetInput(input.values, pressed);
                        if (frame.HasPhase(ScriptPhase::FixedUpdate) &&
                            !frame.Run(ScriptPhase::FixedUpdate, fixedStep, error))
                            return false;
                        auto fixedInput = input;
                        fixedInput.pressed = pressed;
                        fixedInput.jump = jump;
                        if (system && !system->Fixed(fixedStep, fixedInput, frame.Effects(), error))
                            return false;
                        if (system)
                            for (auto& event : system->TakeEvents())
                                if (!frame.QueueEvent(std::move(event), error))
                                    return false;
                        remaining = std::max(0.0, remaining - fixedStep);
                        jump = false;
                        pressed.clear();
                        ++ticks;
                        ++steps;
                    }
                    if (remaining + 1e-10 >= fixedStep)
                    {
                        remaining = std::fmod(remaining, fixedStep);
                        Engine::CpuProfiler::Add("Fixed backlog dropped", 0);
                    }
                }
                if (components)
                {
                    frame.SetInput(input.values, input.pressed);
                    if (system && !system->BeforeUpdate(seconds, input, error))
                        return false;
                    if (!frame.Run(ScriptPhase::Update, seconds, error))
                        return false;
                    if (system && !system->Update(std::min(seconds, .1), frame.Effects(), error))
                        return false;
                    if (system)
                        for (auto& event : system->TakeEvents())
                            if (!frame.QueueEvent(std::move(event), error))
                                return false;
                    latePhase = frame.HasPhase(ScriptPhase::LateUpdate);
                    if (latePhase && !frame.Run(ScriptPhase::LateUpdate, seconds, error))
                        return false;
                }
            }
            if (system && latePhase && !system->Late(frame.Effects(), error))
                return false;
            if (system)
                for (auto& event : system->TakeEvents())
                    if (!frame.QueueEvent(std::move(event), error))
                        return false;
            if (system)
                system->SetInterpolation(remaining / fixedStep);
            if (!frame.Prepare(error) || !transaction.Prepare(error))
                return false;
            auto delta = transaction.Changes();
            delta.Merge(frame.Changes());
            if (system && !system->Prepare(delta, frame.Effects(), error))
                return false;
            if (system)
                delta.Merge(system->Changes());
            auto publication = impl_->changes;
            publication.Merge(delta);
            {
                Engine::CpuScope commit("Runtime session commit");
                if (!transaction.Commit() || !frame.Commit())
                    std::terminate();
                if (system)
                    system->Commit();
                impl_->pressed.swap(pressed);
                impl_->jump = jump;
                impl_->fixedSeconds = remaining;
                impl_->fixedTicks = ticks;
                impl_->started |= starting || !startOnly;
                impl_->changes.Swap(publication);
            }
            impl_->world.TakeChanges();
            frame.FinishCallbacks();
            if (system)
                system->Finish();
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
            error = "Runtime preparation failed with an unknown exception";
            return false;
        }
    }
    bool RuntimeSession::TickFixed(double seconds, const RuntimeInput& input, std::string& error)
    {
        if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(input.horizontal) ||
            !std::isfinite(input.vertical))
        {
            error = "Invalid runtime fixed input";
            return false;
        }
        for (const auto& [name, value] : input.values)
            if (name.empty() || !std::isfinite(value))
            {
                error = "Invalid runtime action input";
                return false;
            }
        return Execute(seconds, input, true, nullptr, {}, false, error, false);
    }
    bool RuntimeSession::ApplyEffects(const RuntimeScriptEffects& effects, std::string& error)
    {
        return Execute(0, {}, false, nullptr, {}, true, error, false, &effects);
    }
    bool RuntimeSession::Animate(const std::map<std::string, float>& clocks, std::string& error)
    {
        if (impl_->active)
        {
            error = "Reentrant runtime animation update";
            return false;
        }
        bool activeTrack = false;
        const auto view = Read();
        for (const auto entity : view.With<AnimationComponent>())
        {
            const auto animation = view.ComponentLease<AnimationComponent>(entity);
            if (!animation->enabled)
                continue;
            for (const auto& track : animation->tracks)
                if (!track.property.starts_with("ui") && track.property != "opacity" && clocks.contains(track.clock))
                {
                    activeTrack = true;
                    break;
                }
        }
        if (!activeTrack)
        {
            error.clear();
            return true;
        }
        return Execute(0, {}, false, nullptr, {}, true, error, false, nullptr, false, &clocks);
    }
    bool RuntimeSession::Validate(std::string& error) const
    {
        return impl_->scripts.Validate(Read(), error);
    }
    bool RuntimeSession::HasPhase(ScriptPhase phase) const
    {
        return impl_->scripts.HasPhase(Read(), phase);
    }
    bool RuntimeSession::Paused() const noexcept
    {
        return impl_->scripts.Paused();
    }
    void RuntimeSession::DiscardPendingInput() noexcept
    {
        if (!impl_->active)
        {
            impl_->pressed.clear();
            impl_->jump = false;
        }
    }
    bool RuntimeSession::DrainEffects(RuntimeScriptEffects& target) noexcept
    {
        return !impl_->active && impl_->scripts.DrainEffects(target);
    }
    bool RuntimeSession::QueueEvent(ScriptEvent event, std::string& error)
    {
        if (impl_->active)
        {
            error = "Cannot queue events during runtime preparation";
            return false;
        }
        return impl_->scripts.QueueEvent(std::move(event), error);
    }
    void RuntimeSession::SetSystem(std::unique_ptr<RuntimeSystem> system)
    {
        if (impl_->active)
            throw std::logic_error("Cannot replace an active runtime system");
        impl_->system = std::move(system);
    }
    void RuntimeSession::SetContent(std::shared_ptr<const Engine::ContentContext> content, std::filesystem::path root)
    {
        if (impl_->active)
            throw std::logic_error("Cannot replace active runtime content");
        impl_->scripts.SetContent(std::move(content), std::move(root));
    }
    SceneChangeSet RuntimeSession::TakeChanges() noexcept
    {
        SceneChangeSet result;
        if (!impl_->active)
            result.Swap(impl_->changes);
        return result;
    }
    RuntimeScriptEffects RuntimeSession::TakeEffects()
    {
        if (impl_->active)
            throw std::logic_error("Cannot drain pending runtime effects");
        return impl_->scripts.TakeEffects();
    }
    double RuntimeSession::Interpolation() const noexcept
    {
        return impl_->fixedSeconds / StepSeconds(impl_->world.Read().Definition()->Source().settings.fixedStep);
    }
    std::uint64_t RuntimeSession::FixedTicks() const noexcept
    {
        return impl_->fixedTicks;
    }
}
