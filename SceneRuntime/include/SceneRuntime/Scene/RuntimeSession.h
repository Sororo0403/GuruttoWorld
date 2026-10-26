#pragma once
#include <SceneRuntime/Scripts/RuntimeScripts.h>

namespace SceneRuntime
{
    struct RuntimePose
    {
        EntityHandle entity;
        LocalTransform transform;
    };
    struct RuntimeInput
    {
        std::map<std::string, float> values;
        std::map<std::string, bool> pressed;
        float horizontal = 0, vertical = 0;
        bool jump = false;
    };

    /// <summary>物理・Animation・表示準備を同じ候補へ参加させる境界です。CPU側は描画APIを所有しません。</summary>
    class RuntimeSystem
    {
    public:
        class Frame
        {
        public:
            /// <summary>確定されなかった内部候補を破棄します。</summary>
            virtual ~Frame() = default;
            /// <summary>固定Scriptより前の姿勢を、補間用の候補に保存します。</summary>
            virtual void BeforeFixed() {}
            /// <summary>固定更新後の候補へ物理と移動を反映します。</summary>
            virtual bool Fixed(double seconds, const RuntimeInput& input, const RuntimeScriptEffects& effects,
                               std::string& error) = 0;
            /// <summary>通常Script更新後、LateUpdate前の候補へAnimationなどを反映します。</summary>
            virtual bool Update(double seconds, const RuntimeScriptEffects& effects, std::string& error) = 0;
            /// <summary>通常Script更新より前に、入力とRotatorなどの実行値を候補へ反映します。</summary>
            virtual bool BeforeUpdate(double, const RuntimeInput&, std::string&)
            {
                return true;
            }
            /// <summary>LateUpdateの書き込みを反映して、時計を進めずに姿勢と制約を再評価します。</summary>
            virtual bool Late(const RuntimeScriptEffects&, std::string&)
            {
                return true;
            }
            /// <summary>CPU成果物を候補で再取得します。失敗時は従来の実行世代を保持します。</summary>
            virtual bool ReloadAssets(std::string&)
            {
                return true;
            }
            /// <summary>未公開の固定時計を、表示抽出の補間比率へ渡します。</summary>
            virtual void SetInterpolation(double) {}
            /// <summary>表示・UI・外部資源の公開候補を準備します。失敗しても公開状態は保持します。</summary>
            virtual bool Prepare(const SceneChangeSet& changes, const RuntimeScriptEffects& effects,
                                 std::string& error) = 0;
            /// <summary>確保や利用者コールバックなしで、準備済みの内部状態だけを公開します。</summary>
            virtual void Commit() noexcept = 0;
            /// <summary>全状態の公開後に予約した外部処理を実行します。</summary>
            virtual void Finish() noexcept = 0;
            /// <summary>数値姿勢以外の、準備中の実行状態の変更を取得します。</summary>
            virtual SceneChangeSet Changes() const
            {
                return {};
            }
            /// <summary>今回の段階で発生した物理・Animationイベントを一度だけ取得します。</summary>
            virtual std::vector<ScriptEvent> TakeEvents()
            {
                return {};
            }
        };
        /// <summary>セッションより長く保持する外部システムを解放します。</summary>
        virtual ~RuntimeSystem() = default;
        /// <summary>このフレーム専用の、未公開の内部状態を準備します。</summary>
        virtual std::unique_ptr<Frame> Begin(WorldState::Transaction& transaction) = 0;
        /// <summary>実行中の物理状態への読み取り専用問い合わせを取得します。</summary>
        virtual RuntimeScriptQueries Queries() const = 0;
    };

    class RuntimeSession final
    {
    public:
        /// <summary>不変定義から、独立したWorld・Script・固定時計を所有するセッションを作ります。</summary>
        explicit RuntimeSession(
            std::shared_ptr<const WorldDefinition> definition,
            std::shared_ptr<const RuntimeScriptDefinitions> scripts = RuntimeScriptRegistry::Capture());
        /// <summary>実行中のScriptを停止し、このセッションの値を解放します。</summary>
        ~RuntimeSession();
        /// <summary>World識別子と実行時計を暗黙に複製しません。</summary>
        RuntimeSession(const RuntimeSession&) = delete;
        /// <summary>実行中の所有関係を暗黙に置き換えません。</summary>
        RuntimeSession& operator=(const RuntimeSession&) = delete;
        /// <summary>確定済みの実行値だけを取得します。</summary>
        WorldState::ReadView Read() const noexcept;
        /// <summary>開始処理を候補で実行し、表示準備と一括確定します。</summary>
        bool Start(std::string& error);
        /// <summary>固定・通常・Animation・LateUpdateを進め、全段階成功時だけ一括確定します。</summary>
        bool Tick(double seconds, const RuntimeInput& input, bool advancePhysics, std::string& error);
        /// <summary>通常段階を実行せず、固定更新と物理だけを同じ取引で進めます。</summary>
        bool TickFixed(double seconds, const RuntimeInput& input, std::string& error);
        /// <summary>ホストからの実行指示を検証し、時計を進めずに一括確定します。</summary>
        bool ApplyEffects(const RuntimeScriptEffects& effects, std::string& error);
        /// <summary>時計を進めず、保存された演出トラックの値と表示準備を一括確定します。</summary>
        bool Animate(const std::map<std::string, float>& clocks, std::string& error);
        /// <summary>時計とScriptを進めず、CPU成果物と表示資源の再取得を同じ取引で確定します。</summary>
        bool ReloadAssets(std::string& error, const SceneEdit* edit = nullptr);
        /// <summary>保持している定義世代で、現在のScript構成を検証します。</summary>
        bool Validate(std::string& error) const;
        /// <summary>現在の構成に指定した実行段階が存在するかを返します。</summary>
        bool HasPhase(ScriptPhase phase) const;
        /// <summary>確定済みの一時停止状態を返します。</summary>
        bool Paused() const noexcept;
        /// <summary>固定段階で未消費の押下とジャンプを破棄します。</summary>
        void DiscardPendingInput() noexcept;
        /// <summary>生成・破棄・追加sceneを含む明示的な定義変更を、Script開始と一緒に確定します。</summary>
        bool Edit(const SceneEdit& edit, std::string& error, bool start = false);
        /// <summary>Pauseコールバックを候補で実行し、成功時だけ状態を切り替えます。</summary>
        bool Pause(bool paused, std::string& error);
        /// <summary>未実行のイベントを次の可変更新へ予約します。</summary>
        bool QueueEvent(ScriptEvent event, std::string& error);
        /// <summary>構造と参照を変更する候補に、ホストが固定したアセット解決を適用します。</summary>
        void SetDefinitionPreparation(std::function<void(SceneLayout&)> prepare);
        /// <summary>次の取引を開始する前に、物理・表示の参加先を設定します。</summary>
        void SetSystem(std::unique_ptr<RuntimeSystem> system);
        /// <summary>このセッションが使用する内容世代と素材ルートを固定します。</summary>
        void SetContent(std::shared_ptr<const Engine::ContentContext> content, std::filesystem::path root);
        /// <summary>確定済みの未取得通知を一度だけ取得します。</summary>
        SceneChangeSet TakeChanges() noexcept;
        /// <summary>確定済みの未取得音声・scene・UI指示を一度だけ取得します。</summary>
        RuntimeScriptEffects TakeEffects();
        /// <summary>公開後の指示を、確保せずに呼び出し側の領域へ移します。</summary>
        bool DrainEffects(RuntimeScriptEffects& target) noexcept;
        /// <summary>描画補間に用いる固定時計の残量比率を取得します。</summary>
        double Interpolation() const noexcept;
        /// <summary>成功した固定更新の総数を取得します。</summary>
        std::uint64_t FixedTicks() const noexcept;

    private:
        struct Impl;
        /// <summary>開始・編集・Pauseも通常tickと同じ準備・確定経路へ渡します。</summary>
        bool Execute(double seconds, const RuntimeInput& input, bool advancePhysics, const SceneEdit* edit,
                     std::optional<bool> paused, bool startOnly, std::string& error, bool components = true,
                     const RuntimeScriptEffects* effects = nullptr, bool startAfterEdit = false,
                     const std::map<std::string, float>* clocks = nullptr, bool reloadAssets = false);
        std::unique_ptr<Impl> impl_;
    };
}
