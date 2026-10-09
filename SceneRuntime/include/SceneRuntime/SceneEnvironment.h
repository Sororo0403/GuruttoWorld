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
        const SceneWorld& World() const { return world_; }
        /// <summary>ロード済みシーン数によらず実行環境の初期化状態を返します。</summary>
        bool Initialized() const { return renderer_!=nullptr; }
        SceneWorld& World() { return world_; }
        double MotionSeconds() const { return seconds_; }
        bool MotionEnabled() const { return motionEnabled_; }
        std::array<float,3> CameraPosition() const;
        std::array<float,4> Particle(unsigned int index) const;
        UiState& Ui() { return uiState_; }
        bool StartAudio(const std::filesystem::path& root,std::string& error) { return audio_.Initialize(root,world_.Layout(),error); }
        void UpdateAudio(bool active) { audio_.Update(world_.Layout(),uiState_,active); }
        void AudioCue(const std::string& cue) { audio_.Cue(cue); }
        void PauseAudio(bool paused) { audio_.Pause(paused); }
        UiEvent Click(const std::string& object);
        /// <summary>実行状態が変化したUI文字列のGPUリソースを描画前に準備します。</summary>
        bool PrepareUi(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,std::string& error) { return presentation_.PrepareUi(renderer,root,world_.Layout(),error,uiState_); }
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
        void Update(double deltaSeconds, bool enabled, bool active);
        bool QueueScriptEvent(ScriptEvent event,std::string& error) { return world_.QueueScriptEvent(std::move(event),error); }
        void SetInputActions(std::map<std::string,float> values,std::map<std::string,bool> pressed) { world_.SetInputActions(std::move(values),std::move(pressed)); }
        /// <summary>未処理の入力を破棄し、Pause・フォーカス復帰で再実行されないようにします。</summary>
        void DiscardPendingInput() { world_.DiscardPendingInput(); DiscardPreparedUi(); physicsAttempted_=false; }
        /// <summary>実行中の個体のAnimatorパラメーターを上書きします。</summary>
        bool SetAnimatorParameter(const std::string& id,const std::string& name,float value) { return world_.SetAnimatorParameter(id,name,value); }
        /// <summary>個体のAnimatorパラメーター上書きを解除します。</summary>
        bool ClearAnimatorParameter(const std::string& id,const std::string& name) { return world_.ClearAnimatorParameter(id,name); }
        bool MovePlayers(double seconds, float horizontal, float vertical, bool jump=false)
        { physicsAttempted_=true; return physicsSucceeded_=world_.MovePlayers(seconds,horizontal,vertical,jump); }
        bool AddImpulse(const std::string& id,const std::array<float,3>& impulse) { return world_.AddImpulse(id,impulse); }
        /// <summary>時計を指定して演出をプレビューします。未開始の開始演出には負値を指定します。</summary>
        void SeekAnimation(float sceneSeconds, float motionSeconds, float startSeconds = -1);
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height, const Engine::Camera* sceneCamera=nullptr, bool showSceneUi=true) const;
    private:
        /// <summary>Script更新のUI候補を現在の描画資源から分離して準備します。</summary>
        bool PrepareRuntimeUi(const SceneLayout&,const ScriptUiCommands&,std::string& error);
        /// <summary>成功したフレームのbinding変更と準備したUI資源を確定します。</summary>
        void ApplyUiCommands(ScriptUiCommands commands);
        /// <summary>失敗したフレームのUI予約と候補描画資源を破棄します。</summary>
        void DiscardPreparedUi();
        /// <summary>候補の所属と配置をリソース準備後に反映します。</summary>
        bool ApplySceneLayout(SceneLayout layout,SceneCollection scenes,std::string& error,bool resetUi=false);
        /// <summary>成功したフレームのシーン操作をGPU描画前に反映します。</summary>
        void ProcessSceneCommands();
        /// <summary>コールバック、ナビゲーション、剛体・キャラクター・骨に固定更新が必要か判定します。</summary>
        static bool RequiresFixedUpdate(const SceneLayout& layout);
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
        SceneWorld world_;
        ScenePresentation presentation_;
        double seconds_=0;
        bool motionEnabled_=true;
        bool physicsAttempted_=false,physicsSucceeded_=true;
        float sceneSeconds_=0, startSeconds_=-1;
    };
}
