#pragma once
#include <SceneRuntime/ScenePresentation.h>
#include <SceneRuntime/SceneAudio.h>
#include <SceneRuntime/SceneCollection.h>

namespace SceneRuntime
{
    class SceneEnvironment final
    {
    public:
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
            const std::filesystem::path& scenePath, std::string& error);
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
            SceneLayout layout, std::string& error);
        /// <summary>開始時に固定した参照情報を実行Worldと共有して初期化します。</summary>
        bool Initialize(const Engine::DirectX12Renderer& renderer,
                        std::shared_ptr<const Engine::ContentContext> context, SceneLayout layout, std::string& error)
        {
            world_.SetContentContext(context);
            return Initialize(renderer, context->Root(), std::move(layout), error);
        }
        /// <summary>実行セッションが所有する固定した参照情報を取得します。</summary>
        const std::shared_ptr<const Engine::ContentContext>& Content() const
        {
            return world_.Content();
        }
        /// <summary>公開前の開始処理を確定し、失敗時のUIと音声予約を破棄します。</summary>
        bool StartScripts(std::string& error)
        {
            if (!world_.StartScripts(error))
            {
                DiscardPreparedUi();
                return false;
            }
            ApplyUiCommands(world_.TakeUiCommands());
            return true;
        }
        const SceneWorld& World() const { return world_; }
        /// <summary>ロード済みシーン数によらず実行環境の初期化状態を返します。</summary>
        uint64_t SceneRevision() const {return sceneRevision_;}
        const std::string& ActiveSceneName() const {return activeSceneName_;}
        bool FirstTitleVisit(const std::string& key) {return visitedTitles_.insert(key).second;}
        bool Initialized() const { return renderer_!=nullptr; }
        SceneWorld& World() { return world_; }
        double MotionSeconds() const { return seconds_; }
        bool MotionEnabled() const { return motionEnabled_; }
        std::array<float,3> CameraPosition() const;
        std::array<float,4> Particle(unsigned int index) const;
        UiState& Ui() { return uiState_; }
        bool StartAudio(const std::filesystem::path& root,std::string& error) { return audio_.Initialize(root,world_.Layout(),error); }
        void UpdateAudio(bool active) { world_.PauseScripts(!active); audio_.Update(world_.Layout(),uiState_,active,&world_); }
        /// <summary>フレーム成功後に鳴らす音を予約します。</summary>
        void AudioCue(const std::string& cue)
        {
            QueueAudio({AudioCommandKind::Cue, cue});
        }
        /// <summary>成功した更新の音声予約を一度だけ実行します。</summary>
        void CommitAudioCommands();
        /// <summary>音声予約の実行先をホストへ委譲します。空なら標準音声を使用します。</summary>
        void SetAudioCommandHandler(std::function<void(const AudioCommand&)> handler) { audioCommandHandler_=std::move(handler); }
        void PauseAudio(bool paused) { world_.PauseScripts(paused); audio_.Pause(paused); }
        UiEvent Click(const std::string& object);
        /// <summary>実行状態が変化したUI文字列のGPUリソースを描画前に準備します。</summary>
        bool Prepare(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,std::string& error) { return presentation_.Prepare(renderer,root,world_.Layout(),error,uiState_,false); }
        /// <summary>画面のポインター入力をUI操作とScriptイベントへ反映します。</summary>
        UiEvent UiPointer(unsigned int width,unsigned int height,float x,float y,bool down,bool pressed,bool released,float wheel=0);
        /// <summary>確定した文字を入力欄とScriptイベントへ反映します。</summary>
        UiEvent UiTextInput(char32_t character);
        /// <summary>保存シーンを追加または保持対象を残して置換します。GPU完了後に呼びます。</summary>
        bool LoadScene(const std::filesystem::path& scene,bool additive,std::string& error);
        /// <summary>指定したロード済みシーンを保持対象以外アンロードします。</summary>
        bool UnloadScene(const std::string& name,std::string& error);
        /// <summary>ロード済みシーンの所属を取得します。</summary>
        const SceneCollection& Scenes() const { return scenes_; }
        /// <summary>UIが予約したシーン操作を描画命令の記録前に適用します。</summary>
        void FlushSceneChanges() { ProcessSceneCommands(); }
        /// <summary>実行更新の成功を返し、成功した場合だけ音声予約を確定可能にします。</summary>
        bool Update(double deltaSeconds, bool enabled, bool active);
        bool QueueScriptEvent(ScriptEvent event,std::string& error) { return world_.QueueScriptEvent(std::move(event),error); }
        void SetInputActions(std::map<std::string,float> values,std::map<std::string,bool> pressed) { world_.SetInputActions(std::move(values),std::move(pressed)); }
        /// <summary>未処理の入力を破棄し、Pause・フォーカス復帰で再実行されないようにします。</summary>
        void DiscardPendingInput()
        {
            world_.DiscardPendingInput();
            DiscardPreparedUi();
            physicsAttempted_ = false;
            movement_.reset();
        }
        /// <summary>実行中の個体のAnimatorパラメーターを上書きします。</summary>
        bool SetAnimatorParameter(const std::string& id,const std::string& name,float value) { return world_.SetAnimatorParameter(id,name,value); }
        /// <summary>個体のAnimatorパラメーター上書きを解除します。</summary>
        bool ClearAnimatorParameter(const std::string& id,const std::string& name) { return world_.ClearAnimatorParameter(id,name); }
        bool MovePlayers(double seconds, float horizontal, float vertical, bool jump=false)
        { physicsAttempted_=true; return physicsSucceeded_=world_.MovePlayers(seconds,horizontal,vertical,jump); }
        /// <summary>入力の固定更新を次のフレーム取引へ予約し、入力準備中はWorldを変更しません。</summary>
        void PrepareMovement(float horizontal, float vertical, bool jump)
        {
            movement_ = Movement{horizontal, vertical, jump};
        }
        bool AddImpulse(const std::string& id,const std::array<float,3>& impulse) { return world_.AddImpulse(id,impulse); }
        /// <summary>時計を指定して演出をプレビューします。未開始の開始演出には負値を指定します。</summary>
        void SeekAnimation(float sceneSeconds, float motionSeconds, float startSeconds = -1);
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height, const Engine::Camera* sceneCamera=nullptr, bool showSceneUi=true) const;
    private:
        void ApplyCameraFocus(const MenuConfiguration* menu);

        /// <summary>Script更新のUI候補を現在の描画資源から分離して準備します。</summary>
        bool PrepareRuntimeUi(const SceneLayout&,const ScriptUiCommands&,std::string& error);
        /// <summary>成功したフレームのbinding変更と準備したUI資源を確定します。</summary>
        void ApplyUiCommands(const ScriptUiCommands& commands);
        /// <summary>失敗したフレームのUI予約と候補描画資源を破棄します。</summary>
        void DiscardPreparedUi();
        /// <summary>候補の所属と配置をリソース準備後に反映します。</summary>
        bool ApplySceneLayout(SceneLayout layout,SceneCollection scenes,std::string& error,bool resetUi=false);
        /// <summary>成功したフレームのシーン操作をGPU描画前に反映します。</summary>
        void ProcessSceneCommands();
        /// <summary>コールバック、ナビゲーション、剛体・キャラクター・骨に固定更新が必要か判定します。</summary>
        static bool RequiresFixedUpdate(const SceneLayout& layout);
        uint64_t sceneRevision_=0;
        std::string activeSceneName_;
        std::set<std::string> visitedTitles_;
        SceneCollection scenes_;
        std::vector<SceneCommand> uiSceneCommands_;
        const Engine::DirectX12Renderer* renderer_=nullptr;
        std::filesystem::path root_;
        /// <summary>保存した移動クリップを現在位置から評価し、メニュー選択へ追従します。</summary>
        void AnimateCameraFocus(float elapsed);
        void SetTransitionState(float progress);
        std::array<float,3> focusPosition_{},focusRotation_{};
        float focusSeconds_=0;
        float focusRequested_=0;
        bool focusEngaged_=false;
        UiState uiState_;
        const UiState* uiPreparationOverride_=nullptr;
        std::optional<SceneUi> pendingUiResources_;
        SceneAudio audio_;
        /// <summary>UI・演出の音声予約を上限付きで保持します。</summary>
        void QueueAudio(AudioCommand command);
        std::vector<AudioCommand> pendingAudio_, committedAudio_;
        bool audioReady_ = false;
        std::function<void(const AudioCommand&)> audioCommandHandler_;
        /// <summary>ポインターとActionによるUI操作を同じスクリプトイベントへ通知します。</summary>
        void NotifyUiEvent(const UiEvent& event);
        SceneWorld world_;
        ScenePresentation presentation_;
        double seconds_=0;
        bool motionEnabled_=true;
        bool physicsAttempted_=false,physicsSucceeded_=true;
        struct Movement
        {
            float horizontal = 0, vertical = 0;
            bool jump = false;
        };
        std::optional<Movement> movement_;
        float sceneSeconds_=0, startSeconds_=-1;
    };
}
