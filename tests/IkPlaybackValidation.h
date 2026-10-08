#pragma once
#include "SkinningValidation.h"
#include "../Editor/src/GameSession.h"
#include "../Editor/src/IkPanel.h"
#include <SceneRuntime/SceneEnvironment.h>

namespace IkPlaybackValidation
{
    using EnvironmentValidation::Require;
    inline void Runtime(Engine::DirectX12Renderer& renderer)
    {
        using namespace Engine; using namespace SceneRuntime; using namespace DirectX;
        const auto content=std::filesystem::absolute("Content"); std::string error;
        const auto scene=SceneLayout::Load(content/"Assets/Scenes/IkPlayground.json",content);
        const auto authored=scene.Serialize();
        Require(SceneLayout::Parse(scene.Serialize()).Serialize()==scene.Serialize(),"IK scene roundtrip");
        const auto rig=Skeleton::Load(content/"Assets/Models/IkBox.gltf",error); Require(rig!=nullptr,error.c_str());
        const auto tip=std::ranges::find_if(rig->nodes,[](const auto& node) { return node.name=="Tip"; });
        Require(tip!=rig->nodes.end(),"IK helper tip imports"); const auto tipIndex=static_cast<size_t>(tip-rig->nodes.begin());
        SceneWorld world; Require(world.Initialize(renderer,content,scene,content/"Shaders/Mesh.hlsl",&error),error.c_str());
        Require(world.UpdateComponents(.1),"Script drives IK through SceneWorld");
        const auto* status=world.AnimatorStatus("Ik2");
        if (!status) throw std::runtime_error("IK actor status missing");
        Require(status->ikOverrides.contains("reach"),"World retains Script IK target override");
        const auto matrices=Skeleton::Matrices(*rig,status->pose); XMFLOAT4X4 objectWorld;
        Require(world.WorldMatrix("Ik2",objectWorld),"IK actor world transform"); XMFLOAT3 endpoint;
        XMStoreFloat3(&endpoint,XMVector3TransformCoord(XMVectorSet(matrices[tipIndex]._41,matrices[tipIndex]._42,matrices[tipIndex]._43,1),
            XMLoadFloat4x4(&rig->inverseRoot)*XMMatrixScaling(rig->importScale,rig->importScale,rig->importScale)*XMLoadFloat4x4(&objectWorld)));
        const auto target=status->ikOverrides.at("reach").target;
        Require(std::abs(endpoint.x-target[0])+std::abs(endpoint.y-target[1])+std::abs(endpoint.z-target[2])<.001f,"Script-controlled tip reaches world target");
        const auto time=status->time; Require(world.UpdateComponents(.1) && world.AnimatorStatus("Ik2")->time>time,"IK target update keeps animation clock");
        ModelRenderer model; Require(model.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),content/"Assets/Models/IkBox.gltf",content/"Shaders/Mesh.hlsl"),"IK GPU model prepares");
        const auto posed=world.AnimatorStatus("Ik2")->pose; Require(model.ApplyPose(posed),"IK pose applies to GPU model");
        const auto paletteMatrices=Skeleton::Matrices(*model.Rig(),posed);
        std::vector<std::unique_ptr<MeshRenderer>> cpu;
        for (const auto& mesh : model.Rig()->meshes)
        {
            auto rendererMesh=std::make_unique<MeshRenderer>();
            Require(rendererMesh->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),Skeleton::Skin(*model.Rig(),mesh,paletteMatrices),content/"Shaders/Mesh.hlsl"),"IK CPU oracle prepares");
            cpu.push_back(std::move(rendererMesh));
        }
        const auto bounds=model.Bounds(); const float scale=.6f/std::max({bounds.Extents.x,bounds.Extents.y,bounds.Extents.z});
        XMFLOAT4X4 display,projection;
        XMStoreFloat4x4(&display,XMMatrixTranslation(-bounds.Center.x,-bounds.Center.y,-bounds.Center.z)*XMMatrixScaling(scale,scale,scale)*XMMatrixTranslation(0,0,.5f));
        XMStoreFloat4x4(&projection,XMMatrixIdentity()); DirectionalLight light; light.enabled=false;
        const auto expected=SkinningValidation::Capture(renderer,[&](auto* commands) { for (const auto& mesh : cpu) mesh->Draw(commands,display,projection,light); });
        const auto actual=SkinningValidation::Capture(renderer,[&](auto* commands) { model.Draw(commands,display,projection,light); });
        Require(actual==expected,"IK GPU image matches CPU posed geometry");
        Require(static_cast<size_t>(std::ranges::count_if(actual,[](unsigned char value) { return value!=0; }))>actual.size()/4+20,"IK GPU image contains visible geometry");
        Require(model.ApplyPose(world.AnimatorStatus("Ik0")->pose),"zero-weight IK pose applies");
        const auto uncorrected=SkinningValidation::Capture(renderer,[&](auto* commands) { model.Draw(commands,display,projection,light); });
        Require(uncorrected!=actual,"IK weights change actual rendered geometry");
        if (!ScriptRegistry::Definitions().contains("IkPlaybackFailure"))
        {
            ScriptDefinition failure; failure.fields={{"repair",{0,0,1}}};
            failure.update=[](ScriptContext& context) {
                context.state["calls"]+=1; context.object.name=std::to_string(context.state["calls"]);
                const auto& definition=context.object.animator->ik.front(); AnimatorIkTarget target{definition.target,definition.hint,1,true};
                if (context.Value("repair")==0) target.target={-1000000,0,0};
                context.scene->SetIkTarget(context.object.id,definition.name,target);
            };
            Require(ScriptRegistry::Register("IkPlaybackFailure",std::move(failure)),"IK rollback driver registers");
        }
        auto failingScene=scene; failingScene.objects.back().scripts={{"failure",true,"IkPlaybackFailure",{}}};
        SceneWorld transactional; Require(transactional.Initialize(renderer,content,failingScene,content/"Shaders/Mesh.hlsl",&error),error.c_str());
        const auto beforeFailure=transactional.Layout().Serialize(); const auto beforePose=transactional.AnimatorStatus("Ik1")->pose;
        Require(!transactional.UpdateComponents(.1) && transactional.Layout().Serialize()==beforeFailure && transactional.AnimatorStatus("Ik1")->time==0 &&
            transactional.AnimatorStatus("Ik1")->pose[2].rotation==beforePose[2].rotation && transactional.AnimatorStatus("Ik1")->ikOverrides.empty(),"late IK failure preserves earlier poses, clocks, overrides and Script edits");
        auto repaired=transactional.Layout().objects.back(); repaired.scripts[0].parameters["repair"]=1;
        Require(transactional.SetComponents(repaired.id,repaired,content,error) && transactional.UpdateComponents(.1),"IK update retries after repair");
        Require(transactional.Layout().objects.back().name=="1.000000" && transactional.AnimatorStatus("Ik1")->time>0,"failed IK frame does not consume Script state or time");
        SceneEnvironment app; Require(app.Initialize(renderer,content,scene,error),error.c_str()); app.Update(.1,true,true);
        Require(app.World().AnimatorStatus("Ik2")->ikOverrides.contains("reach"),"App updates Script IK");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { app.Draw(commands,64,32); })!=RenderResult::Failed && renderer.WaitForIdle(),"App draws IK comparison");
        Editor::GameSession session; Require(session.Play(renderer,content,scene,error) && session.Update(.1,true) && session.Pause(),"Editor plays and pauses IK");
        const auto paused=session.Runtime()->World().AnimatorStatus("Ik2")->time;
        Require(!session.Update(.1,true) && session.Runtime()->World().AnimatorStatus("Ik2")->time==paused,"paused IK clock remains frozen");
        Require(session.Step() && session.Runtime()->World().AnimatorStatus("Ik2")->time>paused,"Editor steps Script IK");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { session.Draw(commands,64,32); })!=RenderResult::Failed && renderer.WaitForIdle(),"Editor draws IK comparison");
        Require(session.Stop() && scene.Serialize()==authored,"Editor Stop preserves authored IK targets");
#if defined(_DEBUG)
        Require(renderer.Render({0,0,0,1},[](auto*,float) {},[&] {
            ImGui::Begin("IK Inspector validation"); auto animator=*scene.objects.back().animator;
            ImGui::SetNextItemOpen(true,ImGuiCond_Always); Editor::IkPanel::Draw(animator,rig.get()); ImGui::End();
        })!=RenderResult::Failed && renderer.WaitForIdle(),"IK Inspector balances ImGui stack");
#endif
    }
}
