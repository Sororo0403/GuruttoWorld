#include <SceneRuntime/ScriptModuleApi.h>
#include <algorithm>
#include <cmath>

namespace
{
    using namespace SceneRuntime;
    float Distance(const ScenePlacement& first,const ScenePlacement& second)
    {
        const auto& a=first.position; const auto& b=second.position;
        return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
    }
    void SetText(ScriptContext& context,const std::string& id,const std::string& text)
    {
        const auto* source=context.scene->Find(id);
        if (!source || !source->text || source->text->text==text) return;
        auto changed=*source; changed.text->text=text; context.scene->SetComponents(id,changed);
    }
    bool VisibleTarget(ScriptContext& context,const ScenePlacement& target)
    {
        std::array<float,3> direction{};
        for (size_t axis=0;axis<3;++axis) direction[axis]=target.position[axis]-context.object.position[axis];
        const float distance=Distance(context.object,target);
        if (distance<0.001f) return true;
        for (auto& value:direction) value/=distance;
        const auto hit=context.scene->Raycast(context.object.position,direction,distance,0xffffffffu,false,context.object.id);
        return !hit || hit->object==target.id;
    }
    void Respawn(ScriptContext& context)
    {
        context.object.position={context.state["spawnX"],1.05f,context.state["spawnZ"]};
        context.state["invulnerable"]=1.5f; context.state["floatTime"]=0;
        context.state["resetVelocity"]=1;
        context.state["hp"]=3;
    }
    void Collect(ScriptContext& context)
    {
        for (const auto& id:context.scene->FindByName("ParkCoin")) {
            const auto* coin=context.scene->Find(id);
            if (!coin || Distance(context.object,*coin)>1.25f || !VisibleTarget(context,*coin)) continue;
            context.scene->Destroy(id); context.state["coins"]+=1;
        }
        for (const auto& id:context.scene->FindByName("ParkSwitch")) {
            const auto* item=context.scene->Find(id);
            if (!item || Distance(context.object,*item)>1.55f || !VisibleTarget(context,*item)) continue;
            context.scene->Destroy(id); context.state["switches"]+=1;
        }
        if (context.state["switches"]>=2 && context.scene->Find("park-gate")) context.scene->Destroy("park-gate");
        for (const auto& id:context.scene->FindByName("ParkCheckpoint")) {
            const auto* item=context.scene->Find(id);
            if (item && Distance(context.object,*item)<1.6f) {
                context.state["spawnX"]=item->position[0]; context.state["spawnZ"]=item->position[2];
            }
        }
    }
    void Combat(ScriptContext& context)
    {
        context.state["attack"]=std::max(0.0f,context.state["attack"]-static_cast<float>(context.seconds));
        context.state["cooldown"]=std::max(0.0f,context.state["cooldown"]-static_cast<float>(context.seconds));
        if (context.Pressed("Attack") && context.state["cooldown"]==0) {
            context.state["attack"]=0.22f; context.state["cooldown"]=0.4f;
        }
        for (const auto& id:context.scene->FindByName("ParkEnemy")) {
            const auto* enemy=context.scene->Find(id);
            if (!enemy) continue;
            const float distance=Distance(context.object,*enemy);
            if (context.state["attack"]>0 && distance<3.1f && VisibleTarget(context,*enemy)) {
                context.scene->Destroy(id); context.state["defeated"]+=1;
            } else if (distance<1.15f && context.state["invulnerable"]==0 && VisibleTarget(context,*enemy)) {
                context.state["hp"]-=1; context.state["invulnerable"]=1.5f;
            }
        }
        if (context.state["hp"]<=0) Respawn(context);
        context.scene->SetUiValue("parkAttack",context.state["attack"]>0 ? 1.0f : 0.0f);
    }
    ScriptDefinition Player()
    {
        ScriptDefinition result;
        result.fields={{"floatDuration",{0.85f,0,2}}};
        result.dataFields={{"scene",{std::string("Assets/Scenes/ParkAdventure.json")}}};
        result.start=[](ScriptContext& context) {
            context.state["hp"]=3; context.state["spawnX"]=context.object.position[0];
            context.state["spawnZ"]=context.object.position[2];
        };
        result.fixedUpdate=[](ScriptContext& context) {
            if (!context.scene || !context.object.playerController) return;
            auto& controller=*context.object.playerController;
            const auto ground=context.scene->Raycast(context.object.position,{0,-1,0},0.8f,0xffffffffu,false,context.object.id);
            const float dt=static_cast<float>(context.seconds);
            if (ground && ground->normal[1]>0.7f) { context.state["floatTime"]=0; context.state["airTime"]=0; }
            else context.state["airTime"]+=dt;
            const bool floating=!ground && context.state["airTime"]>0.25f && context.Input("Jump")>0.5f &&
                !context.Pressed("Jump") && context.state["floatTime"]<context.Value("floatDuration",0.85f);
            controller.useGravity=!floating && context.state["resetVelocity"]==0;
            if (floating) context.state["floatTime"]+=dt;
            context.state["resetVelocity"]=0;
            if (context.state["clear"]>0) controller.moveSpeed=0;
        };
        result.update=[](ScriptContext& context) {
            if (!context.scene) return;
            if (context.Pressed("Restart")) {
                context.scene->LoadScene(std::get<std::string>(context.Data("scene")->value)); return;
            }
            if (context.Pressed("Cancel")) { context.scene->LoadScene("Assets/Scenes/TitleStreet.json"); return; }
            if (context.state["clear"]>0) return;
            const float dt=static_cast<float>(context.seconds);
            context.state["elapsed"]+=dt;
            context.state["invulnerable"]=std::max(0.0f,context.state["invulnerable"]-dt);
            if (context.object.position[1]<-3 || std::abs(context.object.position[0])>20) Respawn(context);
            const float x=context.Input("MoveRight")-context.Input("MoveLeft");
            const float z=context.Input("MoveForward")-context.Input("MoveBack");
            if (std::hypot(x,z)>0.1f) context.object.rotation[1]=std::atan2(x,z);
            Collect(context); Combat(context);
            SetText(context,"park-stats","ライフ "+std::to_string(static_cast<int>(context.state["hp"]))+
                "   花のかけら "+std::to_string(static_cast<int>(context.state["coins"]))+" / 12   スイッチ "+
                std::to_string(static_cast<int>(context.state["switches"]))+" / 2");
            const float progress=context.object.position[2];
            const char* hint=progress<14 ? "花のかけらを集めながら、公園の奥へ進もう" :
                progress<31 ? "段差の上にも寄り道。Spaceでジャンプ" :
                progress<49 ? "水路を飛び越えよう。ジャンプ中にSpaceを押し続けると空中停止" :
                "左右のスイッチを踏んで、展望台への扉を開こう";
            SetText(context,"park-hint",hint);
            const auto* goal=context.scene->Find("park-goal");
            if (goal && context.state["switches"]>=2 && Distance(context.object,*goal)<2.0f) {
                context.state["clear"]=1; context.scene->SetUiValue("parkClear",1);
                context.scene->SetUiValue("parkAttack",0);
                SetText(context,"park-result","公園をクリア！  "+std::to_string(static_cast<int>(context.state["elapsed"]))+
                    " 秒 / 花のかけら "+std::to_string(static_cast<int>(context.state["coins"]))+" / 12\nRで再挑戦・Escでタイトルへ");
            }
        };
        return result;
    }
    ScriptDefinition Camera()
    {
        ScriptDefinition result;
        result.dataFields={{"target",{ScriptObjectReference{"park-player"}}}};
        result.fields={{"distance",{13,8,25}},{"height",{10,5,20}},{"response",{6,1,20}}};
        result.lateUpdate=[](ScriptContext& context) {
            const auto* target=context.Reference("target");
            if (!target || !context.scene || !target->parentId.empty() || !context.object.parentId.empty()) return;
            const float z=target->position[2];
            const float distance=context.Value("distance",13);
            const float height=context.Value("height",10);
            const float blend=1-std::exp(-context.Value("response",6)*static_cast<float>(context.seconds));
            const std::array<float,3> wanted{target->position[0]*0.55f,height,std::clamp(z,2.0f,72.0f)-distance};
            for (size_t axis=0;axis<3;++axis) context.object.position[axis]+=(wanted[axis]-context.object.position[axis])*blend;
            const float dx=target->position[0]-context.object.position[0];
            const float dz=z+3-context.object.position[2];
            context.object.rotation={std::atan2(height-0.8f,std::hypot(dx,dz)),std::atan2(dx,dz),0};
        };
        return result;
    }
    ScriptDefinition Patrol()
    {
        ScriptDefinition result;
        result.fields={{"amplitude",{1,0,2}},{"frequency",{0.25f,0,2}}};
        result.start=[](ScriptContext& context) { context.state["originX"]=context.object.position[0]; };
        result.update=[](ScriptContext& context) {
            context.state["time"]+=static_cast<float>(context.seconds);
            context.object.position[0]=context.state["originX"]+std::sin(context.state["time"]*6.2831853f*context.Value("frequency",0.25f))*context.Value("amplitude",1);
        };
        return result;
    }
}

bool Register_ParkAdventure(void* owner,SceneRuntime::RegisterModuleScript add)
{
    const auto player=Player(),camera=Camera(),patrol=Patrol();
    return add(owner,"ParkPlayer",&player) && add(owner,"ParkCamera",&camera) && add(owner,"ParkPatrol",&patrol);
}
