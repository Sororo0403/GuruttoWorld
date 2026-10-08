#pragma once
#include "EnvironmentValidation.h"
#include "../Editor/src/GameSession.h"
#include "../Editor/src/BlendTreePanel.h"
#include <SceneRuntime/Prefab.h>
#include <limits>

namespace BlendTreeValidation
{
    using EnvironmentValidation::Require;
    inline bool Near(const std::vector<Engine::BonePose>& first,const std::vector<Engine::BonePose>& second)
    {
        if (first.size()!=second.size()) return false;
        for (size_t node=0;node<first.size();++node)
        {
            for (size_t axis=0;axis<3;++axis)
                if (std::abs(first[node].position[axis]-second[node].position[axis])>1e-4f || std::abs(first[node].scale[axis]-second[node].scale[axis])>1e-4f) return false;
            float dot=0; for (size_t axis=0;axis<4;++axis) dot+=first[node].rotation[axis]*second[node].rotation[axis];
            if (std::abs(std::abs(dot)-1)>1e-4f) return false;
        }
        return true;
    }
    inline Engine::SkeletonData Rig()
    {
        Engine::SkeletonData rig; rig.nodes={{"root",-1,{}}};
        for (int index=0;index<3;++index)
        {
            Engine::SkeletalClip clip; clip.name=index==0 ? "Idle" : index==1 ? "Walk" : "Jump"; clip.duration=static_cast<float>(1<<index);
            Engine::BoneTrack track; track.positions={{0,{0,0,0}},{clip.duration,{index==1 ? 4.0f : 0.0f,index==2 ? 8.0f : 0.0f,0}}};
            const float angle=index==1 ? DirectX::XM_PIDIV4 : index==2 ? -DirectX::XM_PIDIV4 : 0;
            track.rotations={{0,{0,0,0,1}},{clip.duration,{0,0,std::sin(angle),std::cos(angle)}}};
            clip.tracks[0]=track; rig.clips.push_back(clip);
        }
        return rig;
    }
    inline SceneRuntime::AnimatorBlendMotion Motion(const std::string& clip,float threshold=0,std::array<float,2> position={},const std::string& parameter="weight")
    {
        SceneRuntime::AnimatorBlendMotion result; result.clip=clip; result.threshold=threshold; result.position=position; result.parameter=parameter; return result;
    }
    inline void Schema()
    {
        using namespace SceneRuntime;
        const auto rig=Rig();
        AnimatorBlendTree one; one.name="one"; one.parameterX="x"; one.children={Motion("Jump",1),Motion("Idle",0),Motion("Walk",.5f)};
        const auto weights=BlendTree::Samples({one},"one",{{"x",.25f}});
        Require(weights.size()==2 && weights[0].weight==.5f && weights[1].weight==.5f,"unsorted 1D thresholds interpolate adjacent clips");
        Require(std::abs(BlendTree::Duration(weights,rig)-1.5)<1e-6,"1D uses weighted synchronized duration");
        const auto mixed=BlendTree::SamplePose(weights,rig,.5,true);
        Require(std::abs(mixed[0].position[0]-1)<1e-5f && std::abs(mixed[0].rotation[2]-std::sin(DirectX::XM_PI/16))<1e-5f,"1D position and spherical quaternion interpolation use common normalized phase");
        Require(Near(mixed,BlendTree::SamplePose(weights,rig,2.5,true)),"looping Blend Tree preserves common phase over multiple cycles");
        Require(BlendTree::SamplePose(weights,rig,10,false)[0].position[0]==2,"nonlooping Blend Tree clamps every child at its end");
        Require(BlendTree::Samples({one},"one",{{"x",-1.0f}}).front().clip=="Idle" && BlendTree::Samples({one},"one",{{"x",10.0f}}).front().clip=="Jump","1D clamps below and above thresholds");
        AnimatorBlendTree two; two.name="two"; two.type=AnimatorBlendType::Cartesian2D; two.parameterX="x"; two.parameterY="y";
        two.children={Motion("Idle",0,{0,0}),Motion("Walk",0,{1,0}),Motion("Jump",0,{0,1})};
        const auto center=BlendTree::Samples({two},"two",{{"x",.5f},{"y",.5f}});
        for (const auto& child : center) Require(std::abs(child.weight-1.0f/3)<1e-6f,"2D weights normalize the linear coordinate bands");
        Require(BlendTree::Samples({two},"two",{{"x",1.0f},{"y",0.0f}}).front().clip=="Walk","2D selects exact sample coordinate");
        const auto centerPose=BlendTree::SamplePose(center,rig,.5,true);
        Require(std::abs(centerPose[0].position[0]-2.0f/3)<1e-5f && std::abs(centerPose[0].position[1]-4.0f/3)<1e-5f,"2D blends all active motion positions");
        const auto neighbor=BlendTree::SamplePose(BlendTree::Samples({two},"two",{{"x",.501f},{"y",.5f}}),rig,.5,true);
        Require(std::abs(neighbor[0].position[0]-centerPose[0].position[0])<.01f,"2D moves continuously near blending boundary");
        const auto remote=BlendTree::Samples({two},"two",{{"x",(std::numeric_limits<float>::max)()},{"y",(std::numeric_limits<float>::max)()}});
        float total=0; for (const auto& sample : remote) { Require(std::isfinite(sample.weight),"remote finite 2D parameter has finite weights"); total+=sample.weight; }
        Require(std::abs(total-1)<1e-6f,"remote 2D inputs still normalize");
        AnimatorBlendTree direct; direct.name="direct"; direct.type=AnimatorBlendType::Direct;
        direct.children={Motion("Idle",0,{},"a"),Motion("Walk",0,{},"b"),Motion("Jump",0,{},"c")};
        const auto directWeights=BlendTree::Samples({direct},"direct",{{"a",2.0f},{"b",1.0f},{"c",1.0f}});
        const auto directPose=BlendTree::SamplePose(directWeights,rig,.5,true);
        Require(directWeights[0].weight==.5f && directWeights[1].weight==.25f && std::abs(directPose[0].position[0]-.5f)<1e-6f && std::abs(directPose[0].position[1]-1)<1e-6f,"Direct normalizes parameter weights and mixes all clips");
        float rotationLength=0; for (const float value : directPose[0].rotation) rotationLength+=value*value;
        Require(std::abs(rotationLength-1)<1e-6f,"multi-motion quaternion remains normalized");
        auto equivalentRig=rig; auto negative=rig.clips[1]; negative.name="NegWalk";
        for (auto& key : negative.tracks[0].rotations) for (auto& value : key.value) value=-value;
        equivalentRig.clips.push_back(negative); auto equivalentTree=direct; equivalentTree.children[2].clip="NegWalk";
        Require(Near(mixed,BlendTree::SamplePose(BlendTree::Samples({equivalentTree},"direct",{{"a",2.0f},{"b",1.0f},{"c",1.0f}}),equivalentRig,.5,true)),"multi-motion quaternion blending treats antipodal rotations as equivalent");
        auto turningRig=rig;
        for (size_t index=1;index<turningRig.clips.size();++index)
        {
            const float angle=(index==1 ? 1.0f : -1.0f)*130*DirectX::XM_PI/360;
            turningRig.clips[index].tracks[0].rotations.back().value={0,0,std::sin(angle),std::cos(angle)};
        }
        const auto nearEdge=BlendTree::SamplePose(BlendTree::Samples({two},"two",{{"x",.99999f},{"y",.99999f}}),turningRig,1,false);
        const auto onEdge=BlendTree::SamplePose(BlendTree::Samples({two},"two",{{"x",1.0f},{"y",1.0f}}),turningRig,1,false);
        Require(Near(nearEdge,onEdge),"quaternion blending stays continuous when a third motion weight reaches zero");
        Require(BlendTree::Samples({direct},"direct",{{"a",-1.0f}}).front().clip.empty(),"missing, zero and negative Direct weights fall back to rest pose");
        AnimatorBlendTree nested=one; nested.name="nested"; nested.children={Motion("",0),Motion("Jump",1)};
        nested.children[0].blendTree="two"; nested.children[0].speed=2;
        const auto leaves=BlendTree::Samples({nested,two},"nested",{{"x",.5f},{"y",.5f}});
        Require(leaves.size()==4 && std::abs(leaves[0].weight-1.0f/6)<1e-6f && leaves[0].speed==2 && leaves.back().weight==.5f,"nested tree multiplies child weight and speed");
        const auto rejects=[&](const auto& trees) { bool failed=false; try { BlendTree::Validate(trees,&rig); } catch (const std::exception&) { failed=true; } Require(failed,"invalid Blend Tree rejected"); };
        auto invalid=one; invalid.children[0].threshold=0; rejects(std::vector{invalid});
        invalid=two; invalid.children[1].position={0,0}; rejects(std::vector{invalid});
        invalid=one; invalid.children[0].clip="unknown"; rejects(std::vector{invalid});
        invalid=one; invalid.children[0].speed=0; rejects(std::vector{invalid});
        invalid=one; invalid.children[0].threshold=NAN; rejects(std::vector{invalid});
        invalid=nested; invalid.children[0].blendTree="nested"; rejects(std::vector{invalid});
        invalid=nested; rejects(std::vector{invalid});
        auto cycle=two; cycle.children[0].clip.clear(); cycle.children[0].blendTree="nested"; rejects(std::vector{nested,cycle});
        std::vector<AnimatorBlendTree> deep;
        for (int depth=0;depth<9;++depth) { AnimatorBlendTree tree; tree.name="depth"+std::to_string(depth); tree.children={Motion("Idle")}; if (depth>0) { tree.children[0].clip.clear(); tree.children[0].blendTree=deep.back().name; } deep.push_back(tree); }
        rejects(deep);
        std::vector<AnimatorBlendTree> expansion;
        for (int depth=0;depth<7;++depth) { AnimatorBlendTree tree; tree.name="branch"+std::to_string(depth); for (int child=0;child<3;++child) { auto motion=Motion(depth==0 ? "Idle" : "",static_cast<float>(child)); if (depth>0) motion.blendTree=expansion.back().name; tree.children.push_back(motion); } expansion.push_back(tree); }
        rejects(expansion);
        AnimatorComponent animator; animator.states[0].blendTree="one"; animator.blendTrees={one}; AnimatorState state;
        Animator::Advance(animator,state,rig,.1,{{"x",.25f}});
        Require(std::abs(state.normalizedTime-.1/1.5)<1e-7 && std::abs(state.pose[0].position[0]-.1f/1.5f*2)<1e-6f,"Animator advances weighted tree clock and pose");
        const auto phase=state.normalizedTime; Animator::Advance(animator,state,rig,0,{{"x",1.0f}});
        Require(state.normalizedTime==phase && state.motions.front().clip=="Jump","parameter changes preserve current normalized phase");
        animator.enabled=false; const auto frozen=state.pose; Animator::Advance(animator,state,rig,.1,{{"x",0.0f}});
        Require(state.normalizedTime==phase && Near(frozen,state.pose),"disabled tree keeps clock and pose frozen"); animator.enabled=true;
        bool rejected=false; const auto current=state.current;
        try { Animator::Advance(animator,state,rig,.1,{{"x",NAN}}); } catch (const std::exception&) { rejected=true; }
        Require(rejected && state.current==current && state.normalizedTime==phase && Near(frozen,state.pose),"invalid input preserves Animator state atomically");
        animator.parameters={{"x",.25f}}; AnimatorState defaults; Animator::Advance(animator,defaults,rig,.1,{});
        Require(defaults.motions.size()==2,"saved parameters drive tree without input binding");
        defaults.parameterOverrides={{"x",1.0f}}; Animator::Advance(animator,defaults,rig,0,{{"x",.25f}});
        Require(defaults.motions.front().clip=="Jump","instance overrides take precedence over actions and saved defaults");
        auto invalidParameters=animator; invalidParameters.parameters={{"",0.0f}};
        rejected=false; try { Animator::Validate(invalidParameters); } catch (const std::exception&) { rejected=true; } Require(rejected,"empty saved parameter name rejected");
        invalidParameters.parameters={{"x",NAN}};
        rejected=false; try { Animator::Validate(invalidParameters); } catch (const std::exception&) { rejected=true; } Require(rejected,"nonfinite saved parameter value rejected");
        animator.states.push_back({"Jump","Jump",1,true}); animator.transitions={{"Idle","Jump","",">",0,.2f,.25f}}; state={};
        for (int frame=0;frame<4;++frame) Animator::Advance(animator,state,rig,.1,{{"x",.25f}});
        Require(state.current=="Idle","tree exit time waits for normalized phase");
        Animator::Advance(animator,state,rig,.1,{{"x",.25f}});
        Require(state.current=="Jump" && state.blendElapsed>0 && state.blendElapsed<state.blendDuration,"tree transitions retain crossfade and exit time");
        const auto layout=SceneLayout::Load("Content/Assets/Scenes/BlendTreePlayground.json");
        const auto restored=SceneLayout::Parse(layout.Serialize());
        for (size_t object=0;object<layout.objects.size();++object) Require(layout.objects[object].animator==restored.objects[object].animator,"all Blend Tree definitions roundtrip scene JSON");
        const auto original=SceneLayout::Load("Content/Assets/Scenes/AnimatorPlayground.json");
        Require(original.objects.back().animator->blendTrees.empty() && original.objects.back().animator->states.front().blendTree.empty(),"legacy Animator format remains compatible");
        auto json=Engine::Json::parse(layout.Serialize());
        auto& treeJson=json["objects"].back()["components"].back()["blendTrees"][0]; treeJson["type"]="unknown";
        rejected=false; try { SceneLayout::Parse(json.dump()); } catch (const std::exception&) { rejected=true; } Require(rejected,"unknown JSON tree type rejected");
        treeJson["type"]="Direct"; treeJson["children"][0]=5;
        rejected=false; try { SceneLayout::Parse(json.dump()); } catch (const std::exception&) { rejected=true; } Require(rejected,"non-object motion JSON rejected");
        const auto prefab=Prefab::Extract(layout,"Player"); SceneLayout instances;
        const auto instance=Prefab::Instantiate(instances,prefab,"Assets/Prefabs/Blend.prefab",{0,0,0});
        Require(instances.objects.front().animator==prefab.objects.front().animator,"Prefab preserves nested blend definitions");
        instances.objects.front().animator->blendTrees[0].children[1].speed=2;
        Require(!Prefab::Overrides(instances,instance).empty(),"Blend Tree properties participate in Prefab overrides");
    }
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        const auto content=std::filesystem::absolute("Content"); auto scene=SceneLayout::Load(content/"Assets/Scenes/BlendTreePlayground.json",content); std::string error;
        SceneWorld world; Require(world.Initialize(renderer,content,scene,content/"Shaders/Mesh.hlsl",&error),error.c_str());
        world.SetInputActions({{"MoveRight",.5f},{"MoveForward",.5f}},{});
        Require(world.UpdateComponents(.05),"scene drives blend weights from action values");
        for (const auto* id : {"Player","Cartesian","Direct"})
        {
            const auto* state=world.AnimatorStatus(id); Require(state && state->motions.size()>1 && state->normalizedTime>0,"runtime mixes multiple clips in each mode");
            const auto rig=Engine::Skeleton::Load(content/"Assets/Models/AnimatedBox.gltf",error);
            Require(rig && Near(state->pose,BlendTree::SamplePose(state->motions,*rig,state->normalizedTime,true)),"runtime pose matches evaluated weighted clips");
        }
        const auto playerBefore=world.AnimatorStatus("Player")->normalizedTime;
        std::string copy; Require(world.DuplicateObject("Player",{0,0,3},copy,error),"tree instance duplicates");
        Require(world.AnimatorStatus(copy)->normalizedTime==playerBefore,"duplicate preserves blend phase");
        const auto originalLayout=world.Layout().Serialize();
        Require(world.SetAnimatorParameter(copy,"speed",0) && world.UpdateComponents(.05),"runtime parameter controls independent instance");
        Require(world.AnimatorStatus(copy)->motions.front().clip=="Idle" && world.AnimatorStatus("Player")->motions.size()>1 && world.Layout().Serialize()==originalLayout,"runtime parameter overrides leave sibling pose and authored values independent");
        Require(!world.SetAnimatorParameter(copy,"speed",NAN) && !world.SetAnimatorParameter("missing","speed",0) && world.AnimatorStatus(copy)->parameterOverrides.at("speed")==0,"invalid parameter changes preserve existing override");
        Require(world.ClearAnimatorParameter(copy,"speed") && world.UpdateComponents(.05) && world.AnimatorStatus(copy)->motions.size()>1,"clearing parameter restores action-driven weights");
        auto disabled=world.Layout().objects.back(); disabled.animator->enabled=false;
        Require(world.SetComponents(copy,disabled,content,error),"duplicate Animator disables independently");
        const auto frozen=world.AnimatorStatus(copy)->normalizedTime;
        Require(world.UpdateComponents(.05) && world.AnimatorStatus(copy)->normalizedTime==frozen && world.AnimatorStatus("Player")->normalizedTime>playerBefore,"independent tree clocks update on shared geometry");
        auto invalid=world.Layout().objects[world.Layout().objects.size()-2]; invalid.animator->blendTrees[0].children[0].clip="missing";
        const auto saved=world.Layout().Serialize(); const auto beforeFailure=world.AnimatorStatus("Direct")->normalizedTime;
        Require(!world.SetComponents("Direct",invalid,content,error) && world.Layout().Serialize()==saved && world.AnimatorStatus("Direct")->normalizedTime==beforeFailure,"invalid tree edits preserve scene and running state");
        auto edited=world.Layout().objects[world.Layout().objects.size()-2]; edited.animator->blendTrees[0].children[0].speed=2; edited.animator->parameters={{"savedWeight",.75f}};
        Editor::EditHistory history; history.Reset({saved,"Direct",{"Direct"}});
        Require(world.SetComponents("Direct",edited,content,error),"Inspector tree edit applies");
        history.Observe({world.Layout().Serialize(),"Direct",{"Direct"}},{});
        Require(world.ReplaceLayout(SceneLayout::Parse(history.Target(false).json),content,error),"Blend Tree Undo applies"); history.Applied(false);
        Require(world.Layout().Serialize()==saved,"Undo restores child speed and tree definitions");
        Require(world.ReplaceLayout(SceneLayout::Parse(history.Target(true).json),content,error),"Blend Tree Redo applies"); history.Applied(true);
        const auto file=std::filesystem::absolute("generated/tests/blend-tree-edited.json"); world.Layout().Save(file);
        Require(SceneLayout::Load(file,content).Serialize()==world.Layout().Serialize(),"edited Blend Tree saves and reloads");
        ScriptDefinition driver; driver.fields={{"value",{.25f,0,1}}};
        driver.update=[](ScriptContext& context) { context.state["tick"]+=1; context.object.position[0]=context.state["tick"]; context.scene->SetAnimatorParameter("Direct","MoveRight",context.Value("value",.25f)); };
        if (!ScriptRegistry::Definitions().contains("ValidationBlendDriver")) Require(ScriptRegistry::Register("ValidationBlendDriver",driver),"Animator parameter driver registers");
        auto drivenScene=scene; ScenePlacement source; source.id="driver"; source.name="Blend driver"; source.scripts={{"script",true,"ValidationBlendDriver",{}}}; drivenScene.objects.push_back(source);
        SceneWorld driven; Require(driven.Initialize(renderer,content,drivenScene,content/"Shaders/Mesh.hlsl",&error),"script-driven tree prepares");
        for (int index=0;index<64;++index) Require(driven.SetAnimatorParameter("Direct","P"+std::to_string(index),0),"64 runtime Animator parameters supported");
        const auto beforeScript=driven.Layout().Serialize();
        Require(!driven.UpdateComponents(.05) && driven.Layout().Serialize()==beforeScript && driven.AnimatorStatus("Direct")->normalizedTime==0,"parameter overflow rolls back script state and scene before commit");
        Require(driven.ClearAnimatorParameter("Direct","P0") && driven.UpdateComponents(.05) && driven.Layout().objects.back().position[0]==1 && driven.AnimatorStatus("Direct")->parameterOverrides.at("MoveRight")==.25f,"successful script parameter change preserves callback state after rejected update");
        const auto scriptedPhase=driven.AnimatorStatus("Direct")->normalizedTime;
        Require(driven.UpdateComponents(.05) && driven.AnimatorStatus("Direct")->normalizedTime>scriptedPhase && driven.Layout().objects.back().position[0]==2,"Script parameter updates do not reset animation clock");
        auto deletedScene=scene; size_t nextId=1; ScriptScene api(deletedScene,nextId);
        api.SetAnimatorParameter("Direct","speed",1); api.Destroy("Direct"); api.Commit();
        Require(api.animatorParameters.empty(),"deleted Animator cancels pending parameter commands");
        const auto authored=scene.Serialize(); Editor::GameSession session;
        Require(session.Play(renderer,content,scene,error),"Editor enters Blend Tree playback"); session.Runtime()->SetInputActions({{"MoveRight",.5f},{"MoveForward",.5f}},{});
        Require(session.Update(.05,true),"Editor advances Blend Tree");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { session.Draw(commands,64,32); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"Editor renders all Blend Tree modes on GPU");
        const auto phase=session.Runtime()->World().AnimatorStatus("Player")->normalizedTime;
        Require(session.Pause() && !session.Update(.1,true) && session.Runtime()->World().AnimatorStatus("Player")->normalizedTime==phase && session.Step(),"tree playback pauses and steps");
        Require(session.Runtime()->World().AnimatorStatus("Player")->normalizedTime>phase,"Step advances normalized tree phase");
        Require(session.Stop() && scene.Serialize()==authored,"stopping Blend Tree restores authored data");
        SceneEnvironment app; Require(app.Initialize(renderer,content,scene,error),"App prepares Blend Tree scene"); app.SetInputActions({{"MoveRight",.5f},{"MoveForward",.5f}},{}); app.Update(.05,true,true);
        Require(app.World().AnimatorStatus("Cartesian")->motions.size()>1,"App action values drive Cartesian blending");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { app.Draw(commands,64,32); })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"App renders Blend Tree scene");
#if defined(_DEBUG)
        Require(renderer.Render({0,0,0,1},[](auto*,float) {},[&] {
            ImGui::Begin("Blend Tree Inspector validation");
            for (const auto& object : scene.objects) if (object.animator)
            {
                ImGui::PushID(object.id.c_str()); auto animator=*object.animator;
                animator.parameters={{"savedWeight",.75f}}; Editor::BlendTreePanel::Parameters(animator);
                ImGui::SetNextItemOpen(true,ImGuiCond_Always); Editor::BlendTreePanel::Draw(animator,nullptr); ImGui::PopID();
            }
            ImGui::End();
        })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"all tree modes and children draw with balanced Inspector stack");
#endif
    }
}
