// スキーマのJSONから生成します。定義の変更はJSONへ記述してください。
#pragma once
#include <SceneRuntime/ScriptRuntime.h>

namespace SceneRuntime::GeneratedSchemas
{
    /// <summary>宣言の既定値と型を実行定義へ設定します。</summary>
    inline SceneRuntime::ScriptDefinition ParkCameraSchema()
    {
        static const auto schema = []
        {
            std::string source;
            source.reserve(641);
            source += R"WP1_0({"scripts":{"ParkCamera":{"data":{"target":{"kind":"reference","value":"park-player"}},"parameters":{"distance":{"initial":13.0,"maximum":25.0,"minimum":8.0},"height":{"initial":10.0,"maximum":20.0,"minimum":5.0},"horizontalFollow":{"initial":0.550000011920929,"maximum":1.0,"minimum":0.0},"lookAhead":{"initial":3.0,"maximum":1000.0,"minimum":-1000.0},"lookHeight":{"initial":0.800000011920929,"maximum":1000.0,"minimum":-1000.0},"maxZ":{"initial":72.0,"maximum":100000.0,"minimum":-100000.0},"minZ":{"initial":2.0,"maximum":100000.0,"minimum":-100000.0},"response":{"initial":6.0,"maximum":20.0,"minimum":1.0}},"purposes":{}}},"version":1})WP1_0";
            return SceneRuntime::ScriptSchema::Parse(Engine::Json::parse(source)).at("ParkCamera");
        }();
        SceneRuntime::ScriptDefinition result;
        static_cast<SceneRuntime::ScriptSchema&>(result) = schema;
        return result;
    }
    /// <summary>宣言の既定値と型を実行定義へ設定します。</summary>
    inline SceneRuntime::ScriptDefinition ParkPatrolSchema()
    {
        static const auto schema = []
        {
            std::string source;
            source.reserve(192);
            source += R"WP1_0({"scripts":{"ParkPatrol":{"data":{},"parameters":{"amplitude":{"initial":1.0,"maximum":2.0,"minimum":0.0},"frequency":{"initial":0.25,"maximum":2.0,"minimum":0.0}},"purposes":{}}},"version":1})WP1_0";
            return SceneRuntime::ScriptSchema::Parse(Engine::Json::parse(source)).at("ParkPatrol");
        }();
        SceneRuntime::ScriptDefinition result;
        static_cast<SceneRuntime::ScriptSchema&>(result) = schema;
        return result;
    }
    /// <summary>宣言の既定値と型を実行定義へ設定します。</summary>
    inline SceneRuntime::ScriptDefinition ParkPlayerSchema()
    {
        static const auto schema = []
        {
            std::string source;
            source.reserve(4001);
            source += R"WP1_0({"scripts":{"ParkPlayer":{"data":{"attackAction":{"kind":"string","value":"Attack"},"attackState":{"kind":"string","value":"parkAttack"},"backAction":{"kind":"string","value":"MoveBack"},"cancelAction":{"kind":"string","value":"Cancel"},"checkpoints":{"kind":"array","value":[]},"checkpointsName":{"kind":"string","value":"ParkCheckpoint"},"clearState":{"kind":"string","value":"parkClear"},"coins":{"kind":"array","value":[]},"coinsName":{"kind":"string","value":"ParkCoin"},"enemies":{"kind":"array","value":[]},"enemiesName":{"kind":"string","value":"ParkEnemy"},"forwardAction":{"kind":"string","value":"MoveForward"},"gate":{"kind":"reference","value":"park-gate"},"goal":{"kind":"reference","value":"park-goal"},"hint":{"kind":"reference","value":"park-hint"},"hint1":{"kind":"string","value":"花のかけらを集めながら、公園の奥へ進もう"},"hint2":{"kind":"string","value":"段差の上にも寄り道。Spaceでジャンプ"},"hint3":{"kind":"string","value":"水路を飛び越えよう。ジャンプ中にSpaceを押し続けると空中停止"},"hint4":{"kind":"string","value":"左右のスイッチを踏んで、展望台への扉を開こう"},"jumpAction":{"kind":"string","value":"Jump"},"leftAction":{"kind":"string","value":"MoveLeft"},"restartAction":{"kind":"string","value":"Restart"},"result":{"kind":"reference","value":"park-result"},"resultTemplate":{"kind":"string","value":"公園をクリア！  {elapsed} 秒 / 花のかけら {coins} / {coinTotal}\nRで再挑戦・Escでタイトルへ"},"returnScene":{"kind":"string","value":"Assets/Scenes/TitleStreet.json"},"rightAction":{"kind":"string","value":"MoveRight"},"scene":{"kind":"string","value":"Assets/Scenes/ParkAdventure.json"},"stats":{"kind":"reference","value":"park-stats"},"statsTemplate":{"kind":"string","value":"ライフ {hp}   花のかけら {coins} / {coinTotal}   スイッチ {switches} / {requiredSwitches}"},"switches":{"kind":"array","value":[]},"switchesName":{"kind":"string","value":"ParkSwitch"},"useNameLookup":{"kind":"bool","value":true}},"parameters":{"attackCooldown":{"initial":0.4000000059604645,"maximum":60.0,"minimum":0.009999999776482582},"attackDistance":{"initial":3.0999999046325684,"maximum":1000.0,"minimum":0.009999999776482582},"attackDuration":{"initial":0.2199999988079071,"maximum":60.0,"minimum":0.009999999776482582},"checkpointDistance":{"initial":1.600000023841858,"maximum":1000.0,"minimum":0.009999999776482582},"coinDistance":{"initial":1.25,"maximum":1000.0,"minimum":0.009999999776482582},"coinTotal":{"initial":12.0,"maximum":10000.0,"minimum":0.0},"damage":{"initial":1.0,"maximum":1000.0,"minimum":0.0},"damageDistance":{"initial":1.149999976158142,"maximum":1000.0,"minimum":0.009999999776482582},"fallLimit":{"initial":-3.0,"maximum":100000.0,"minimum":-100000.0},"floatDelay":{"initial":0.25,"maximum":60.0,"minimum":0.0},"floatDuration":{"initial":0.8500000238418579,"maximum":2.0,"minimum":0.0},"goalDistance":{"initial":2.0,"maximum":1000.0,"minimum":0.009999999776482582},"groundNormal":{"initial":0.699999988079071,"maximum":1.0,"minimum":0.0},"groundProbe":{"initial":0.800000011920929,"maximum":100.0,"minimum":0.009999999776482582},"hintBoundary1":{"initial":14.0,"maximum":100000.0,"minimum":-100000.0},"hintBoundary2":{"initial":31.0,"maximum":100000.0,"minimum":-100000.0},"hintBoundary3":{"initial":49.0,"maximum":100000.0,"minimum":-100000.0},"invulnerableDuration":{"initial":1.5,"maximum":60.0,"minimum":0.0},"leftLimit":{"initial":-20.0,"maximum":100000.0,"minimum":-100000.0},"maxHp":{"initial":3.0,"maximum":1000.0,"minimum":1.0},"requiredSwitches":{"initial":2.0,"maximum":1000.0,"minimum":0.0},"respawnHeight":{"initial":1.0499999523162842,"maximum":100000.0,"minimum":-100000.0},"rightLimit":{"initial":20.0,"maximum":100000.0,"minimum":-100000.0},"switchDistance":{"initial":1.5499999523162842,"maximum":1000.0,"minimum":0.009999999776482582}},"purposes":{"returnScene":"sceneAsset","scene":"sceneAsset"}}},"version":1})WP1_0";
            return SceneRuntime::ScriptSchema::Parse(Engine::Json::parse(source)).at("ParkPlayer");
        }();
        SceneRuntime::ScriptDefinition result;
        static_cast<SceneRuntime::ScriptSchema&>(result) = schema;
        return result;
    }
}
