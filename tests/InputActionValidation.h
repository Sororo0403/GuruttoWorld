#pragma once
#include <Engine/Input/InputActions.h>
#include "UiValidation.h"
#include <stdexcept>

namespace InputActionValidation
{
    inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
    inline void Run()
    {
        using namespace Engine;
        auto bindings=InputActions::Defaults();
        Require(InputActions::Parse(InputActions::Serialize(bindings))==bindings,"input action bindings roundtrip");
        InputActions input; InputSnapshot snapshot; snapshot.active=true; snapshot.gamepadConnected=true;
        input.Update(snapshot);
        snapshot.down[DIK_SPACE]=snapshot.pressed[DIK_SPACE]=true; input.Update(snapshot);
        Require(input.Down("Jump") && input.Pressed("Jump"),"keyboard action press");
        snapshot.pressed.fill(false); input.Update(snapshot);
        Require(input.Down("Jump") && !input.Pressed("Jump"),"held action does not repeat press");
        snapshot.down.fill(false); snapshot.stick={0.75f,0}; input.Update(snapshot);
        Require(input.Value("MoveRight")==0.75f && input.Pressed("MoveRight") && !input.Down("MoveLeft"),"analog action preserves amount and sign");
        input.Update(snapshot); Require(!input.Pressed("MoveRight"),"held analog action does not repeat press");
        snapshot.stick={0.1f,0}; input.Update(snapshot); Require(!input.Down("MoveRight"),"analog threshold rejects small input");
        snapshot.buttons=XINPUT_GAMEPAD_A; snapshot.pressedButtons=XINPUT_GAMEPAD_A; input.Update(snapshot);
        Require(input.Pressed("Jump") && input.Pressed("Confirm"),"gamepad button maps to named actions");
        snapshot.active=false; input.Update(snapshot); Require(!input.Down("Jump"),"inactive actions clear");
        snapshot.active=true; input.Update(snapshot); Require(!input.Pressed("Jump"),"restored input suppresses synthetic press");
        bindings["Jump"].keys={DIK_J}; bindings["Jump"].buttons=0; input.SetBindings(bindings);
        snapshot.buttons=snapshot.pressedButtons=0; snapshot.down[DIK_SPACE]=true; input.Update(snapshot);
        Require(!input.Down("Jump"),"rebind disables former key");
        snapshot.down[DIK_J]=snapshot.pressed[DIK_J]=true; input.Update(snapshot); Require(input.Pressed("Jump"),"rebound key activates action");
        auto bad=InputActions::Serialize(bindings); bad["Jump"]["keys"]={256};
        bool rejected=false; try { InputActions::Parse(bad); } catch(const std::exception&) {rejected=true;}
        Require(rejected,"out of range key rejected");
        SceneRuntime::ScenePlacement actor; actor.id="input-actor"; actor.name="Actor";
        actor.scripts.push_back({"script",true,"ValidationInput",{}});
        SceneRuntime::ScriptDefinition definition;
        definition.update=[](SceneRuntime::ScriptContext& context) { context.object.position[0]+=context.Input("MoveRight"); if (context.Pressed("Jump")) context.object.position[1]+=1; };
        Require(SceneRuntime::ScriptRegistry::Register("ValidationInput",definition),"input-aware script registers");
        SceneRuntime::SceneLayout scriptLayout; scriptLayout.objects={actor};
        SceneRuntime::ScriptRuntime runtime; std::string error;
        Require(runtime.Update(scriptLayout,0.1,error,{{"MoveRight",0.75f}},{{"Jump",true}}) && scriptLayout.objects[0].position==std::array<float,3>{0.75f,1,0},"scripts receive named input values and pressed actions");
        runtime.Stop(scriptLayout);
        auto layout=UiValidation::Layout(); layout.objects[1].button->inputAction="Confirm";
        const auto restored=SceneRuntime::SceneLayout::Parse(layout.Serialize());
        SceneRuntime::UiState state;
        Require(SceneRuntime::SceneUi::Shortcut(restored,"action:Confirm",64,32,state)=="panel","Button named action matches");
        layout.objects[1].button->enabled=false;
        Require(SceneRuntime::SceneUi::Shortcut(layout,"action:Confirm",64,32,state).empty(),"disabled Button rejects named action");
    }
}
