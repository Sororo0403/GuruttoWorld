#pragma once
#include <LegacyScripts/ScriptRuntime.h>
#include <SceneRuntime/Scripts/RuntimeScripts.h>

namespace SceneRuntime
{
    /// <summary>SDK移行中のコールバックだけを、一個体の候補を介して型別Worldへ接続します。</summary>
    class LegacyScriptAdapter final
    {
    public:
        /// <summary>現在の登録とDLLの世代を固定し、新SDKの組込動作と統合します。</summary>
        static std::shared_ptr<const RuntimeScriptDefinitions> Capture(
            std::shared_ptr<const ScriptDefinitions> definitions = {});

        /// <summary>移行前の回帰fixtureから、新しいゲームコールバックを検証します。配布対象には含めません。</summary>
        static ScriptCallbacks Adapt(const RuntimeScriptDefinition& definition);

    private:
        /// <summary>テスト内の保存候補を一時的な型付きWorldへ投影します。</summary>
        static void InvokeModern(const std::function<void(RuntimeScriptContext&)>& callback, ScriptContext& context);
        enum class Callback
        {
            Start,
            Update,
            Fixed,
            Late,
            Event,
            PrepareStop,
            Pause
        };
        /// <summary>一つの旧コールバックの変更を、同じ外側の取引へ渡します。</summary>
        static void Invoke(const ScriptCallbacks& callbacks, Callback kind, RuntimeScriptContext& context,
                           bool paused = false);
    };
}
