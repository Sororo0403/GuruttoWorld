#pragma once
#include <SceneRuntime/Scripts/RuntimeScripts.h>
#include <SceneRuntime/Physics/PhysicsWorld.h>
#include <Engine/Assets/ContentContext.h>
#include <algorithm>
#include <utility>

namespace TestSupport
{
    class ModernScriptHarness final
    {
    public:
        /// <summary>手動で物理段階を進めるゲーム検証に、実SDKの状態と予約キューを提供します。</summary>
        ModernScriptHarness() = default;
        /// <summary>最後に確定した個体値を使い、開始済みの処理を停止します。</summary>
        ~ModernScriptHarness()
        {
            if (scripts_)
                scripts_->Stop(*world_);
        }
        /// <summary>検証対象のゲームの参照情報を固定します。</summary>
        void SetContentContext(std::shared_ptr<const Engine::ContentContext> content)
        {
            content_ = std::move(content);
        }
        /// <summary>物理との段階順を維持し、型付きの候補が成功した場合だけ配置へ値を戻します。</summary>
        bool Update(SceneRuntime::SceneLayout& layout, double seconds, std::string& error,
                    const std::map<std::string, float>& input = {}, const std::map<std::string, bool>& pressed = {},
                    const SceneRuntime::PhysicsWorld* physics = nullptr,
                    SceneRuntime::ScriptPhase phase = SceneRuntime::ScriptPhase::Update)
        {
            using namespace SceneRuntime;
            try
            {
                if (!world_)
                {
                    world_ = std::make_unique<WorldState>(WorldDefinition::Compile(layout));
                    scripts_ = std::make_unique<RuntimeScripts>();
                    scripts_->SetContent(content_, content_ ? content_->Root() : std::filesystem::path{});
                    baseline_ = layout;
                }
                {
                    auto transaction = world_->BeginTransaction();
                    const auto changed = SceneChangeSet::Compare(baseline_, layout);
                    const bool definitionChanged =
                        changed.structureChanged || std::any_of(changed.fields.begin(), changed.fields.end(),
                                                                [](const auto& field)
                                                                {
                                                                    return field.componentId.empty();
                                                                });
                    if (definitionChanged)
                    {
                        if (!transaction.ApplyStructure(ReplaceDocumentEdit{layout}, error).succeeded)
                            return false;
                    }
                    else
                    {
                        // 保存配置を使う手動物理との境界でも、数値変更で不変定義を再構築しません。
                        for (size_t index = 0; index < layout.objects.size(); ++index)
                        {
                            const auto& object = layout.objects[index];
                            const auto& previous = baseline_.objects[index];
                            const auto entity = transaction.Read().Resolve(object.id);
                            if (!entity)
                                return false;
                            if ((object.position != previous.position || object.rotation != previous.rotation ||
                                 object.scale != previous.scale) &&
                                !transaction
                                     .SetLocalTransform(*entity, {object.position, object.rotation, object.scale},
                                                        error)
                                     .succeeded)
                                return false;
#define ENGINE_TEST_SYNC_OPTIONAL(Type, member, references, assets, order)                                             \
    if (object.member != previous.member && !transaction.SetComponent<Type>(*entity, object.member, error).succeeded)  \
        return false;
#define ENGINE_TEST_SYNC_REPEATED(Type, member, references, assets, order)                                             \
    if (object.member != previous.member && !transaction.SetRepeated<Type>(*entity, object.member, error).succeeded)   \
        return false;
                            ENGINE_SCENE_COMPONENTS(ENGINE_TEST_SYNC_OPTIONAL, ENGINE_TEST_SYNC_REPEATED)
#undef ENGINE_TEST_SYNC_OPTIONAL
#undef ENGINE_TEST_SYNC_REPEATED
                        }
                    }
                    RuntimeScriptQueries queries;
                    if (physics)
                        queries.raycast = [physics](const auto& origin, const auto& direction, float distance,
                                                    unsigned int mask, bool triggers, const auto& ignore)
                        {
                            return physics->Raycast(origin, direction, distance, mask, triggers, ignore);
                        };
                    auto frame = scripts_->BeginFrame(transaction, input, pressed, std::move(queries));
                    if (!frame.Run(phase, seconds, error) || !frame.Prepare(error) || !transaction.Prepare(error))
                        return false;
                    if (!transaction.Commit() || !frame.Commit())
                        std::terminate();
                    frame.FinishCallbacks();
                }
                layout = world_->Read().ExportLayout();
                baseline_ = layout;
                world_->TakeChanges();
                auto effects = scripts_->TakeEffects();
                scenes_.insert(scenes_.end(), effects.scenes.begin(), effects.scenes.end());
                audio_.insert(audio_.end(), effects.audio.begin(), effects.audio.end());
                for (auto& [key, value] : effects.ui.values)
                    ui_.values[key] = value;
                for (auto& [key, value] : effects.ui.texts)
                    ui_.texts[key] = std::move(value);
                return true;
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
                return false;
            }
        }
        /// <summary>外側の検証候補でも、同じ型付きの成功境界を使います。</summary>
        bool UpdateCandidate(SceneRuntime::SceneLayout& layout, double seconds, std::string& error,
                             const std::map<std::string, float>& input, const std::map<std::string, bool>& pressed,
                             const SceneRuntime::PhysicsWorld* physics, SceneRuntime::ScriptPhase phase,
                             const SceneRuntime::SceneLayout*)
        {
            return Update(layout, seconds, error, input, pressed, physics, phase);
        }
        /// <summary>成功した段階の開始処理は、その段階の確定時に確定済みです。</summary>
        void CommitStarts() noexcept {}
        /// <summary>予約したUI指示を一度だけ取得します。</summary>
        SceneRuntime::ScriptUiCommands TakeUiCommands()
        {
            return std::exchange(ui_, {});
        }
        /// <summary>予約したシーン操作を一度だけ取得します。</summary>
        std::vector<SceneRuntime::SceneCommand> TakeSceneCommands()
        {
            return std::exchange(scenes_, {});
        }
        /// <summary>予約した音声指示を一度だけ取得します。</summary>
        std::vector<SceneRuntime::AudioCommand> TakeAudioCommands()
        {
            return std::exchange(audio_, {});
        }
        /// <summary>明示的な検証終了でも停止処理を重複させません。</summary>
        void Stop(SceneRuntime::SceneLayout&)
        {
            if (scripts_)
                scripts_->Stop(*world_);
        }

    private:
        std::shared_ptr<const Engine::ContentContext> content_;
        std::unique_ptr<SceneRuntime::WorldState> world_;
        std::unique_ptr<SceneRuntime::RuntimeScripts> scripts_;
        SceneRuntime::SceneLayout baseline_;
        SceneRuntime::ScriptUiCommands ui_;
        std::vector<SceneRuntime::SceneCommand> scenes_;
        std::vector<SceneRuntime::AudioCommand> audio_;
    };
}
