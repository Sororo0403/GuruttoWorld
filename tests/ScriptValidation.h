#pragma once
#include <SceneRuntime/ScriptRuntime.h>
#include <Engine/Core/Json.h>
#include <cmath>
#include <stdexcept>

namespace ScriptValidation
{
    inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
    inline void Run()
    {
        using namespace SceneRuntime;
        static int started=0,updated=0,stopped=0;
        started=updated=stopped=0;
        ScriptDefinition definition; definition.fields={{"speed",{2,0,10}}};
        definition.start=[](ScriptContext& context) { ++started; context.state["frames"]=0; };
        definition.update=[](ScriptContext& context) { ++updated; context.state["frames"]+=1; context.object.position[0]+=context.Value("speed",2)*static_cast<float>(context.seconds); };
        definition.stop=[](ScriptContext&) { ++stopped; };
        Require(ScriptRegistry::Register("ValidationMover",definition),"custom script registration");
        Require(!ScriptRegistry::Register("ValidationMover",definition),"duplicate script registration rejected");
        auto invalid=definition; invalid.fields["speed"].initial=20;
        Require(!ScriptRegistry::Register("InvalidMover",invalid),"invalid script metadata rejected");
        ScenePlacement object; object.id="actor"; object.name="Actor";
        object.scripts.push_back({"script",true,"ValidationMover",{{"speed",4.0f}}});
        object.scripts.push_back({"spin",true,"Spin",{{"speed",90.0f}}});
        SceneLayout layout; layout.objects={object};
        const auto restored=SceneLayout::Parse(layout.Serialize());
        Require(restored.objects.back().scripts==object.scripts,"multiple scripts roundtrip");
        Require(object.HasComponentId("script") && object.HasComponentId("spin"),"script IDs participate in uniqueness");
        auto bad=layout; bad.objects[0].scripts[1].id="script";
        bool rejected=false; try { bad.Serialize(); } catch (const std::exception&) { rejected=true; }
        Require(rejected,"duplicate script IDs rejected");
        ScriptRuntime runtime; std::string error;
        Require(runtime.Update(layout,0.1,error) && runtime.Update(layout,0.1,error),"custom scripts update");
        Require(started==1 && updated==2 && std::abs(layout.objects[0].position[0]-0.8f)<0.0001f,"script starts once and uses saved parameters");
        Require(layout.objects[0].rotation[1]>0,"second script runs independently");
        layout.objects[0].scripts[0].enabled=false;
        Require(runtime.Update(layout,0.1,error) && stopped==1,"disable calls script stop once");
        layout.objects[0].scripts[0].enabled=true;
        Require(runtime.Update(layout,0.1,error) && started==2,"reenable starts new script instance");
        runtime.Stop(layout); runtime.Stop(layout);
        Require(stopped==2,"shutdown stops each instance once");
        auto unknown=layout; unknown.objects[0].scripts[0].behaviour="Missing";
        const auto unknownSaved=SceneLayout::Parse(unknown.Serialize());
        Require(unknownSaved.objects[0].scripts[0].behaviour=="Missing","unknown script preserved for later registration");
        Require(!runtime.Update(unknown,0.1,error) && !error.empty(),"unknown runtime script reports error");
    }
}
