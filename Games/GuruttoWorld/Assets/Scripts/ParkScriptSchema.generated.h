// Generated from ParkScriptSchema.schema.json. Edit the JSON schema; builds regenerate this file.
#pragma once
#include <SceneRuntime/ScriptRuntime.h>
namespace SceneRuntime {
inline ScriptDefinition ParkPlayerSchema() {
    ScriptDefinition result;
    result.fields={
        {"floatDuration",{0.85f,0.0f,2.0f}},
        {"maxHp",{3.0f,1.0f,1000.0f}},
        {"invulnerableDuration",{1.5f,0.0f,60.0f}},
        {"attackDuration",{0.22f,0.01f,60.0f}},
        {"attackCooldown",{0.4f,0.01f,60.0f}},
        {"attackDistance",{3.1f,0.01f,1000.0f}},
        {"damageDistance",{1.15f,0.01f,1000.0f}},
        {"damage",{1.0f,0.0f,1000.0f}},
        {"coinDistance",{1.25f,0.01f,1000.0f}},
        {"switchDistance",{1.55f,0.01f,1000.0f}},
        {"checkpointDistance",{1.6f,0.01f,1000.0f}},
        {"requiredSwitches",{2.0f,0.0f,1000.0f}},
        {"coinTotal",{12.0f,0.0f,10000.0f}},
        {"goalDistance",{2.0f,0.01f,1000.0f}},
        {"respawnHeight",{1.05f,-100000.0f,100000.0f}},
        {"fallLimit",{-3.0f,-100000.0f,100000.0f}},
        {"leftLimit",{-20.0f,-100000.0f,100000.0f}},
        {"rightLimit",{20.0f,-100000.0f,100000.0f}},
        {"groundProbe",{0.8f,0.01f,100.0f}},
        {"groundNormal",{0.7f,0.0f,1.0f}},
        {"floatDelay",{0.25f,0.0f,60.0f}},
        {"hintBoundary1",{14.0f,-100000.0f,100000.0f}},
        {"hintBoundary2",{31.0f,-100000.0f,100000.0f}},
        {"hintBoundary3",{49.0f,-100000.0f,100000.0f}},
    };
    result.dataFields={
        {"useNameLookup",{true}},
        {"scene",{std::string("Assets/Scenes/ParkAdventure.json")}},
        {"returnScene",{std::string("Assets/Scenes/TitleStreet.json")}},
        {"coinsName",{std::string("ParkCoin")}},
        {"switchesName",{std::string("ParkSwitch")}},
        {"enemiesName",{std::string("ParkEnemy")}},
        {"checkpointsName",{std::string("ParkCheckpoint")}},
        {"attackAction",{std::string("Attack")}},
        {"restartAction",{std::string("Restart")}},
        {"cancelAction",{std::string("Cancel")}},
        {"jumpAction",{std::string("Jump")}},
        {"rightAction",{std::string("MoveRight")}},
        {"leftAction",{std::string("MoveLeft")}},
        {"forwardAction",{std::string("MoveForward")}},
        {"backAction",{std::string("MoveBack")}},
        {"attackState",{std::string("parkAttack")}},
        {"clearState",{std::string("parkClear")}},
        {"statsTemplate",{std::string("ライフ {hp}   花のかけら {coins} / {coinTotal}   スイッチ {switches} / {requiredSwitches}")}},
        {"resultTemplate",{std::string("公園をクリア！  {elapsed} 秒 / 花のかけら {coins} / {coinTotal}\nRで再挑戦・Escでタイトルへ")}},
        {"hint1",{std::string("花のかけらを集めながら、公園の奥へ進もう")}},
        {"hint2",{std::string("段差の上にも寄り道。Spaceでジャンプ")}},
        {"hint3",{std::string("水路を飛び越えよう。ジャンプ中にSpaceを押し続けると空中停止")}},
        {"hint4",{std::string("左右のスイッチを踏んで、展望台への扉を開こう")}},
        {"gate",{ScriptObjectReference{"park-gate"}}},
        {"goal",{ScriptObjectReference{"park-goal"}}},
        {"stats",{ScriptObjectReference{"park-stats"}}},
        {"hint",{ScriptObjectReference{"park-hint"}}},
        {"result",{ScriptObjectReference{"park-result"}}},
        {"coins",{ScriptValue::Array{}}},
        {"switches",{ScriptValue::Array{}}},
        {"enemies",{ScriptValue::Array{}}},
        {"checkpoints",{ScriptValue::Array{}}},
    };
    result.dataPurposes={
        {"scene",ScriptDataPurpose::SceneAsset},
        {"returnScene",ScriptDataPurpose::SceneAsset},
    };
    return result;
}
inline ScriptDefinition ParkCameraSchema() {
    ScriptDefinition result;
    result.fields={
        {"distance",{13.0f,8.0f,25.0f}},
        {"height",{10.0f,5.0f,20.0f}},
        {"response",{6.0f,1.0f,20.0f}},
        {"horizontalFollow",{0.55f,0.0f,1.0f}},
        {"minZ",{2.0f,-100000.0f,100000.0f}},
        {"maxZ",{72.0f,-100000.0f,100000.0f}},
        {"lookAhead",{3.0f,-1000.0f,1000.0f}},
        {"lookHeight",{0.8f,-1000.0f,1000.0f}},
    };
    result.dataFields={
        {"target",{ScriptObjectReference{"park-player"}}},
    };
    result.dataPurposes={
    };
    return result;
}
inline ScriptDefinition ParkPatrolSchema() {
    ScriptDefinition result;
    result.fields={
        {"amplitude",{1.0f,0.0f,2.0f}},
        {"frequency",{0.25f,0.0f,2.0f}},
    };
    result.dataFields={
    };
    result.dataPurposes={
    };
    return result;
}
}
