#pragma once
#include "BlendTreeValidation.h"
#include "SkinningValidation.h"
#include "../Editor/src/AnimationEventPanel.h"

namespace AnimationEventValidation
{
    using EnvironmentValidation::Require;
    inline SceneRuntime::AnimatorEventKey Key(const std::string& clip,const std::string& name,float time,float minimum=0)
    {
        SceneRuntime::AnimatorEventKey key; key.clip=clip; key.name=name; key.time=time; key.value=2; key.stringValue="left"; key.intValue=7; key.minimumWeight=minimum; return key;
    }
    inline void Schema()
    {
        using namespace SceneRuntime;
        const auto rig=BlendTreeValidation::Rig(); AnimatorComponent animator;
        animator.states[0].clip="Idle";
        animator.events={Key("Idle","end",1),Key("Idle","start",0),Key("Idle","middle",.25f)};
        AnimatorState state; Animator::Advance(animator,state,rig,0,{});
        Require(state.events.empty() && state.eventsAtStart,"preview at zero does not emit events");
        Animator::Advance(animator,state,rig,.1,{});
        Require(state.events.size()==1 && state.events[0].name=="start" && state.events[0].stringValue=="left" && state.events[0].intValue==7 && state.events[0].weight==1,"first positive tick emits start and typed arguments");
        Animator::Advance(animator,state,rig,.1,{}); Require(state.events.empty(),"start does not repeat next tick");
        Animator::Advance(animator,state,rig,.1,{}); Require(state.events.size()==1 && state.events[0].name=="middle","interval crossing emits marker once");
        Animator::Advance(animator,state,rig,0,{}); Require(state.events.empty(),"zero step clears last events without replay");
        const auto boundary=AnimationEvents::Collect(animator.events,rig,"Idle",{{"Idle",1,1}},.9,1.1,true,false);
        Require(boundary.size()==2 && boundary[0].name=="end" && boundary[0].cycle==0 && boundary[1].name=="start" && boundary[1].cycle==1,"loop end precedes next start at exact boundary");
        const auto loops=AnimationEvents::Collect({Key("Idle","step",.25f)},rig,"Idle",{{"Idle",1,1}},.1,3.3,true,false);
        Require(loops.size()==4 && loops[0].cycle==0 && loops.back().cycle==3,"several loops crossed by one tick emit every occurrence");
        Require(AnimationEvents::Collect({Key("Idle","filtered",0,1)},rig,"Idle",{{"Idle",1,1}},0,50000,true,false,0,1).size()==1,"minimum blend weight filters large cycle interval before capacity validation");
        Require(AnimationEvents::Collect({Key("Idle","rounding",.3f)},rig,"Idle",{{"Idle",1,1}},.2,3*static_cast<double>(.1f),true,false).size()==1 &&
            AnimationEvents::Collect({Key("Idle","rounding",.3f)},rig,"Idle",{{"Idle",1,1}},3*static_cast<double>(.1f),.4,true,false).empty(),"float timestamp rounding does not delay or duplicate a marker");
        const auto single=AnimationEvents::Collect(animator.events,rig,"Idle",{{"Idle",1,1}},.9,2.5,false,false);
        Require(single.size()==1 && single[0].name=="end" && AnimationEvents::Collect(animator.events,rig,"Idle",{{"Idle",1,1}},2.5,3,false,false).empty(),"nonlooping end fires once even as clock continues");
        AnimatorBlendTree tree; tree.name="mix"; tree.type=AnimatorBlendType::Direct;
        tree.children={BlendTreeValidation::Motion("Idle",0,{},"a"),BlendTreeValidation::Motion("Idle",0,{},"b"),BlendTreeValidation::Motion("Walk",0,{},"c")};
        animator.blendTrees={tree}; animator.states[0].clip.clear(); animator.states[0].blendTree="mix"; animator.events={Key("Idle","idle",.01f,.5f),Key("Walk","walk",.02f,.5f)};
        state={}; Animator::Advance(animator,state,rig,.1,{{"a",1.0f},{"b",2.0f},{"c",1.0f}});
        Require(state.events.size()==1 && state.events[0].name=="idle" && state.events[0].weight==.75f,"same clip in tree merges weight before minimum-weight filtering");
        animator.enabled=false; const auto phase=state.normalizedTime; Animator::Advance(animator,state,rig,.1,{});
        Require(state.events.empty() && state.normalizedTime==phase,"disabled Animator freezes event timeline");
        animator.enabled=true; animator.states[0].speed=0; Animator::Advance(animator,state,rig,.1,{});
        Require(state.events.empty() && state.normalizedTime==phase,"speed zero does not emit events");
        animator={}; animator.states={{"Idle","Idle",1,true},{"Walk","Walk",1,true}};
        animator.transitions={{"Idle","Walk","go",">",.5f,.2f,-1}}; animator.events={Key("Walk","enter",0),Key("Walk","weighted",.1f,.2f)};
        state={}; Animator::Advance(animator,state,rig,0,{}); Animator::Advance(animator,state,rig,.1,{{"go",1.0f}});
        Require(state.events.size()==2 && state.events[0].name=="enter" && state.events[0].weight==0 && std::abs(state.events[1].weight-.5f)<1e-6f,"transition markers use incoming state and weight at crossing");
        const auto rejects=[&](const auto& component) { bool rejected=false; try { Animator::Validate(component,&rig); } catch (const std::exception&) { rejected=true; } Require(rejected,"invalid event configuration rejected"); };
        auto invalid=animator; invalid.events[0].clip="missing"; rejects(invalid);
        invalid=animator; invalid.events[0].time=3; rejects(invalid);
        invalid=animator; invalid.events[0].time=-1; rejects(invalid);
        invalid=animator; invalid.events[0].minimumWeight=NAN; rejects(invalid);
        invalid=animator; invalid.events[0].name.clear(); rejects(invalid);
        invalid=animator; invalid.events[0].stringValue=std::string(1025,'x'); rejects(invalid);
        animator={}; animator.states[0].clip="Idle"; animator.states[0].speed=1000; animator.events={Key("Idle","many",0)};
        auto shortRig=rig; shortRig.clips[0].duration=.0001f;
        state={}; bool rejected=false; try { Animator::Advance(animator,state,shortRig,.1,{}); } catch (const std::exception&) { rejected=true; }
        Require(rejected && state.current.empty() && state.time==0 && state.eventsAtStart,"event overflow preserves complete Animator state");
        const auto scene=SceneLayout::Load("Content/Assets/Scenes/AnimationEventPlayground.json");
        const auto restored=SceneLayout::Parse(scene.Serialize());
        for (size_t index=0;index<scene.objects.size();++index) Require(scene.objects[index].animator==restored.objects[index].animator,"event arguments and thresholds roundtrip JSON");
        auto json=Engine::Json::parse(scene.Serialize());
        auto& components=json["objects"].back()["components"];
        auto item=std::find_if(components.begin(),components.end(),[](const auto& value) { return value.at("type")=="Animator"; });
        (*item)["events"][0]["intValue"]=18446744073709551615ULL;
        rejected=false; try { SceneLayout::Parse(json.dump()); } catch (const std::exception&) { rejected=true; } Require(rejected,"unsigned integer payload overflow rejected");
        (*item)["events"][0]["intValue"]=1.5;
        rejected=false; try { SceneLayout::Parse(json.dump()); } catch (const std::exception&) { rejected=true; } Require(rejected,"fractional integer payload rejected");
        ScriptRuntime scripts; ScriptEvent malformed{"test","owner","owner",0}; malformed.animation=AnimatorEventOccurrence{};
        rejected=false; try { scripts.QueueEvent(malformed); } catch (const std::exception&) { rejected=true; } Require(rejected,"invalid Script animation metadata rejected");
        const auto prefab=Prefab::Extract(scene,"Player"); SceneLayout instances; const auto id=Prefab::Instantiate(instances,prefab,"Assets/Prefabs/Events.prefab",{0,0,0});
        Require(instances.objects.front().animator->events==prefab.objects.front().animator->events,"Prefab retains event definitions");
        instances.objects.front().animator->events[0].stringValue="overridden"; Require(!Prefab::Overrides(instances,id).empty(),"event payload participates in Prefab property overrides");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        const auto content=std::filesystem::absolute("Content"); std::string error;
        auto scene=SceneLayout::Load(content/"Assets/Scenes/AnimationEventPlayground.json",content);
        SceneWorld world; Require(world.Initialize(renderer,content,scene,content/"Shaders/Mesh.hlsl",&error),error.c_str());
        Require(world.UpdateComponents(.1),"event scene advances first marker");
        Require(world.AnimatorStatus("Player")->events.size()==1 && world.Layout().objects[3].pointLight->intensity==0,"marker queues for following Script update");
        Require(world.UpdateComponents(.05) && world.Layout().objects[3].pointLight->intensity>0,"animation event invokes built-in light pulse");
        std::string copy; Require(world.DuplicateObject("Player",{0,0,3},copy,error),"event actor duplicates at current phase");
        const auto before=world.AnimatorStatus(copy)->normalizedTime;
        Require(world.UpdateComponents(.05) && world.AnimatorStatus(copy)->normalizedTime>before,"duplicate event clock advances independently");
        auto changed=world.Layout().objects.back(); changed.animator->enabled=false;
        Require(world.SetComponents(copy,changed,content,error),"duplicate disables without resetting peers");
        const auto frozen=world.AnimatorStatus(copy)->normalizedTime;
        Require(world.UpdateComponents(.05) && world.AnimatorStatus(copy)->events.empty() && world.AnimatorStatus(copy)->normalizedTime==frozen,"disabled duplicate clears event output and freezes clock");
        const auto saved=world.Layout().Serialize(); Editor::EditHistory history; history.Reset({saved,"Player",{"Player"}});
        changed=world.Layout().objects[3]; changed.animator->events[0].value=3;
        Require(world.SetComponents("Player",changed,content,error),"Inspector event edit applies"); history.Observe({world.Layout().Serialize(),"Player",{"Player"}},{});
        Require(world.ReplaceLayout(SceneLayout::Parse(history.Target(false).json),content,error),"event Undo applies"); history.Applied(false);
        Require(world.Layout().Serialize()==saved,"event Undo restores all fields");
        Require(world.ReplaceLayout(SceneLayout::Parse(history.Target(true).json),content,error),"event Redo applies"); history.Applied(true);
        const auto output=std::filesystem::absolute("generated/tests/events-edited.json"); world.Layout().Save(output);
        Require(SceneLayout::Load(output,content).Serialize()==world.Layout().Serialize(),"events save and reload");
        const auto authored=scene.Serialize(); Editor::GameSession session; Require(session.Play(renderer,content,scene,error),"Editor event playback begins");
        Require(session.Update(.1,true) && session.Pause(),"pause immediately after event detection");
        Require(!session.Update(.1,true) && session.Runtime()->World().Layout().objects[3].pointLight->intensity==0,"paused marker remains pending without Script delivery");
        Require(session.Step() && session.Runtime()->World().Layout().objects[3].pointLight->intensity>0,"Step delivers pending animation event once");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { session.Draw(commands,64,32); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"Editor renders event light pulse and HDR");
        Require(session.Stop() && scene.Serialize()==authored,"Stop preserves authored event timeline");
        SceneEnvironment app; Require(app.Initialize(renderer,content,scene,error),"App prepares event scene"); app.Update(.1,true,true); app.Update(.05,true,true);
        Require(app.World().Layout().objects[3].pointLight->intensity>0,"App delivers event through shared Script path");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { app.Draw(commands,64,32); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"App draws animation event scene");
#if defined(_DEBUG)
        Require(renderer.Render({0,0,0,1},[](auto*,float) {},[&] {
            ImGui::Begin("Animation event inspector validation"); auto animator=*scene.objects[3].animator;
            ImGui::SetNextItemOpen(true,ImGuiCond_Always); Editor::AnimationEventPanel::Draw(animator,nullptr); ImGui::End();
        })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"event Inspector controls balance ImGui stack");
#endif
        // 多数の個体のイベントでキューを超過しても、先に評価した個体・Script・構造変更を反映しません。
        ScriptDefinition spam; spam.fields={{"spawn",{0,0,1}}}; spam.update=[](ScriptContext& context) { context.state["ticks"]+=1; context.object.position[0]=context.state["ticks"]; if (context.Value("spawn",0)>0) { ScenePlacement child; child.name="temporary"; context.scene->Spawn(child); } };
        if (!ScriptRegistry::Definitions().contains("ValidationEventRollback")) Require(ScriptRegistry::Register("ValidationEventRollback",spam),"rollback script registers");
        SceneLayout crowded;
        for (int index=0;index<17;++index)
        {
            auto actor=scene.objects[3]; actor.id="actor"+std::to_string(index); actor.name=actor.id; actor.pointLight.reset(); actor.scripts.clear(); actor.animator->states={{"Idle","Idle",1,true}};
            actor.animator->initialState="Idle"; actor.animator->blendTrees.clear(); actor.animator->events.assign(256,Key("Idle","start",0)); crowded.objects.push_back(actor);
        }
        ScenePlacement source; source.id="source"; source.name="Source"; source.scripts={{"script",true,"ValidationEventRollback",{}}}; crowded.objects.push_back(source);
        SceneWorld full; Require(full.Initialize(renderer,content,crowded,content/"Shaders/Mesh.hlsl",&error),"event capacity scene prepares");
        const auto original=full.Layout().Serialize(); const auto firstPose=full.AnimatorStatus("actor0")->pose;
        Require(!full.UpdateComponents(.1) && full.Layout().Serialize()==original && full.AnimatorStatus("actor0")->time==0 && BlendTreeValidation::Near(firstPose,full.AnimatorStatus("actor0")->pose),"world event overflow preserves earlier poses and Script changes");
        SceneEnvironment rejectedEnvironment; Require(rejectedEnvironment.Initialize(renderer,content,crowded,error),"environment capacity test prepares");
        rejectedEnvironment.Update(.1,true,true);
        Require(rejectedEnvironment.MotionSeconds()==0 && rejectedEnvironment.World().Layout().Serialize()==original,"App environment keeps shared clocks on rejected event update");
        auto fixed=full.Layout().objects[16]; fixed.animator->events.clear(); Require(full.SetComponents(fixed.id,fixed,content,error) && full.UpdateComponents(.1),"4096 events fit after failed frame");
        Require(full.Layout().objects.back().position[0]==1,"failed frame did not advance callback state");
        Require(full.UpdateComponents(.05) && full.AnimatorStatus("actor0")->events.empty(),"processed markers are not replayed");
        // 構造変更を伴う失敗でも新しいIDと旧オブジェクトの姿勢を保持します。
        auto structural=crowded; structural.objects.back().scripts[0].parameters={{"spawn",1.0f}};
        SceneWorld topology; Require(topology.Initialize(renderer,content,structural,content/"Shaders/Mesh.hlsl",&error),"structural event rollback prepares");
        const auto topologyBefore=topology.Layout().Serialize();
        Require(!topology.UpdateComponents(.1) && topology.Layout().Serialize()==topologyBefore && topology.AnimatorStatus("actor0")->time==0,"event overflow rolls back prepared topology and clocks");
        fixed=topology.Layout().objects[16]; fixed.animator->events.clear(); Require(topology.SetComponents(fixed.id,fixed,content,error) && topology.UpdateComponents(.1),"structural update succeeds after event capacity repair");
        Require(topology.Layout().objects.back().id=="runtime-1" && topology.Layout().objects[17].position[0]==1,"failed topology update preserves ID reservation and Script state");
        auto malformed=scene; malformed.objects.push_back(source);
        SceneWorld atomic; Require(atomic.Initialize(renderer,content,malformed,content/"Shaders/Mesh.hlsl",&error),"atomic pose test prepares");
        Require(atomic.SetAnimatorParameter("Player","speed",0),"first actor overrides malformed global speed");
        const auto atomicBefore=atomic.Layout().Serialize(); const auto posed=atomic.AnimatorStatus("Player")->pose;
        atomic.SetInputActions({{"MoveRight",NAN}},{});
        Require(!atomic.UpdateComponents(.1) && atomic.Layout().Serialize()==atomicBefore && atomic.AnimatorStatus("Player")->time==0 && BlendTreeValidation::Near(posed,atomic.AnimatorStatus("Player")->pose),"late actor evaluation failure rolls back earlier model pose, Script and events");
        atomic.SetInputActions({},{}); Require(atomic.UpdateComponents(.1) && atomic.Layout().objects.back().position[0]==1 && atomic.AnimatorStatus("Player")->events.size()==1,"retry advances once after pose evaluation failure");
        ScriptDefinition receiver; receiver.update=[](ScriptContext&) {};
        receiver.onEvent=[](ScriptContext& context) { if (!context.event || !context.event->animation) return; const auto& event=*context.event->animation; context.state["received"]+=1; context.object.position[0]=context.state["received"]; context.object.name=event.clip+"/"+event.stringValue+"/"+std::to_string(event.intValue); };
        if (!ScriptRegistry::Definitions().contains("ValidationAnimationReceiver")) Require(ScriptRegistry::Register("ValidationAnimationReceiver",receiver),"typed animation listener registers");
        SceneLayout receiving; auto owner=scene.objects[3]; owner.animator->initialState="Idle"; owner.animator->states={{"Idle","Idle",1,true}}; owner.animator->blendTrees.clear(); owner.animator->events={Key("Idle","notify",.05f)};
        owner.scripts={{"listener",true,"ValidationAnimationReceiver",{}}}; receiving.objects={owner};
        SceneWorld delivery; Require(delivery.Initialize(renderer,content,receiving,content/"Shaders/Mesh.hlsl",&error) && delivery.UpdateComponents(.1) && delivery.UpdateComponents(.1),"typed event dispatch runs");
        Require(delivery.Layout().objects[0].name=="Idle/left/7" && delivery.Layout().objects[0].position[0]==1 && delivery.UpdateComponents(.1) && delivery.Layout().objects[0].position[0]==1,"listener receives metadata exactly once");
        Require(delivery.RemoveObjects({"Player"},error) && delivery.UpdateComponents(.1),"removed actor discards its remaining event state");
    }
}
