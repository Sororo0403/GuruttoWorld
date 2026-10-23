#pragma once
#include <SceneRuntime/ScriptRuntime.h>
#include <SceneRuntime/SceneWorld.h>
#include <memory>
#include <stdexcept>

namespace ScriptStartValidation
{
    struct Counts {int resources=0,starts=0,stops=0,pauses=0,resumes=0;};
    /// <summary>開始時の外部資源と、失敗後の再試行・候補破棄・一時停止を検証します。</summary>
    inline void Run()
    {
        using namespace SceneRuntime;
        const auto require=[](bool value) {if(!value) throw std::runtime_error("Script start rollback regression");};
        auto counts=std::make_shared<Counts>();ScriptDefinition definition;
        definition.start=[counts](ScriptContext& c) {++counts->resources;++counts->starts;if(c.Pressed("Throw")) throw std::runtime_error("start failure");};
        definition.stop = [counts](const ScriptStopContext&)
        {
            --counts->resources;
            ++counts->stops;
        };
        definition.update=[](ScriptContext& c) {if(c.Pressed("Fail")) throw std::runtime_error("update failure");};
        definition.fixedUpdate=definition.update;
        definition.pause=[counts](ScriptContext&,bool paused) {if(paused) ++counts->pauses;else ++counts->resumes;};
        require(ScriptRegistry::Register("ValidationStartResources",definition));
        ScenePlacement owner;owner.id="resource-owner";owner.scripts.push_back({"script",true,"ValidationStartResources",{}});
        SceneLayout layout;layout.objects={owner};ScriptRuntime runtime;std::string error;
        require(!runtime.Update(layout,.016,error,{},{{"Throw",true}}) && counts->resources==0);
        require(!runtime.Update(layout,.016,error,{},{{"Fail",true}}) && counts->resources==0);
        require(runtime.Update(layout,.016,error) && counts->resources==1);
        require(!runtime.Update(layout,.016,error,{},{{"Fail",true}}) && counts->resources==1);
        runtime.Pause(layout,true);runtime.Pause(layout,true);runtime.Pause(layout,false);
        require(counts->pauses==1 && counts->resumes==1);
        runtime.Stop(layout);require(counts->resources==0 && counts->starts==counts->stops);
        {
            auto candidate=layout;ScriptRuntime pending;
            require(pending.UpdateCandidate(candidate,.016,error) && counts->resources==1);
            {auto copy=pending;require(counts->resources==1);}
            require(counts->resources==1);
        }
        require(counts->resources==0 && counts->starts==counts->stops);
    }
    /// <summary>スクリプト成功後に描画準備が失敗する通常更新と固定更新の資源解放を検証します。</summary>
    inline void Runtime(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        using namespace SceneRuntime;
        auto counts=std::make_shared<Counts>();ScriptDefinition definition;
        definition.start=[counts](ScriptContext&) {++counts->resources;};
        definition.stop = [counts](const ScriptStopContext&)
        {
            --counts->resources;
        };
        definition.update=[](ScriptContext&) {};definition.fixedUpdate=definition.update;
        if(!ScriptRegistry::Register("ValidationOuterStartResources",definition)) throw std::runtime_error("outer start registration");
        ScenePlacement owner;owner.id="outer-resource";owner.scripts.push_back({"script",true,"ValidationOuterStartResources",{}});
        SceneLayout layout;layout.objects={owner};std::string error;
        for(bool fixed:{false,true}) {
            SceneWorld world;
            if(!world.Initialize(renderer,root,layout,root/"Shaders/Mesh.hlsl",&error)) throw std::runtime_error(error);
            world.SetRuntimePreparation([](const auto&,std::string& e) {e="expected resource preparation failure";return false;});
            const auto tick=[&] {return fixed?world.MovePlayers(1.0/60,0,0):world.UpdateComponents(.016);};
            if(tick() || counts->resources!=0) throw std::runtime_error("failed outer transaction leaked start resources");
            world.SetRuntimePreparation({});
            if(!tick() || counts->resources!=1) throw std::runtime_error("outer retry did not acquire resources once");
        }
        if(counts->resources!=0) throw std::runtime_error("outer runtime shutdown leaked resources");
    }
}
