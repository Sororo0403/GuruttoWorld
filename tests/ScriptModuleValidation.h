#pragma once
#include <SceneRuntime/ScriptModule.h>
#include <Engine/Core/Json.h>
#include <SceneRuntime/ProjectSettings.h>
#include "../Editor/src/GameSession.h"
#include <fstream>

namespace ScriptModuleValidation
{
    inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
    inline std::filesystem::path Root() { return std::filesystem::absolute("generated/tests/native-script-project/Content"); }
    inline void Schema()
    {
        using namespace SceneRuntime;
        ScriptDefinition definition; definition.update=[](ScriptContext& c) { c.object.position[0]+=1; };
        std::string error;
        Require(ScriptRegistry::InstallModule("ValidationModuleOwner",{{"ValidationModuleBehaviour",definition}},error),"module registers atomically");
        auto invalid=definition; invalid.fields["bad"]={2,0,1};
        Require(!ScriptRegistry::InstallModule("ValidationModuleOwner",{{"ValidationModuleBehaviour",invalid},{"ValidationPartial",definition}},error),"invalid module rejected");
        Require(ScriptRegistry::Definitions().contains("ValidationModuleBehaviour") && !ScriptRegistry::Definitions().contains("ValidationPartial"),"failed module keeps all previous registrations");
        Require(!ScriptRegistry::InstallModule("ValidationModuleOwner",{{"Spin",definition}},error) && ScriptRegistry::Definitions().contains("ValidationModuleBehaviour"),"module cannot replace built-in behaviour");
        Require(!ScriptRegistry::InstallModule("ValidationSecondOwner",{{"ValidationModuleBehaviour",definition}},error),"modules cannot overwrite each other's names");
        Require(ScriptRegistry::InstallModule("ValidationModuleOwner",{},error) && !ScriptRegistry::Definitions().contains("ValidationModuleBehaviour"),"empty module removes only its own registrations");
        const auto root=std::filesystem::absolute("generated/tests/script-source")/(std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        std::filesystem::path created;
        Require(!ScriptModule::CreateSource(root,"../Escape",created,error),"script source cannot escape source folder");
        Require(ScriptModule::CreateSource(root,"TestMover",created,error) && std::filesystem::exists(created),"C++ script template created");
        std::ifstream input(created); const std::string before(std::istreambuf_iterator<char>(input),{}); input.close();
        Require(before.find("Register_TestMover")!=std::string::npos && before.find("dataFields")!=std::string::npos,"script template contains host registration and public fields");
        Require(!ScriptModule::CreateSource(root,"TestMover",created,error),"script source never overwritten");
        std::ifstream after(created); Require(std::string(std::istreambuf_iterator<char>(after),{})==before,"existing source preserved");
    }
    inline void Setup()
    {
        std::filesystem::create_directories(Root()); std::filesystem::path created; std::string error;
        if (!std::filesystem::exists(Root()/"Assets/Scripts/ValidationNativeMover.cpp"))
            Require(SceneRuntime::ScriptModule::CreateSource(Root(),"ValidationNativeMover",created,error),"native test template created");
        std::filesystem::create_directories(Root()/"Assets/Scenes");
        SceneRuntime::ScenePlacement owner; owner.id="native"; owner.name="Native"; owner.scripts.push_back({"script",true,"ValidationNativeMover",{{"speed",8.0f}}});
        SceneRuntime::SceneLayout scene; scene.objects={owner}; scene.Save(Root()/"Assets/Scenes/Main.json");
        SceneRuntime::ProjectSettings project; project.startupScene="Assets/Scenes/Main.json"; project.width=640; project.height=360; project.Save(Root());
    }
    inline bool RejectUnexpectedRegistration(void*,const char*,const SceneRuntime::ScriptDefinition*) { return false; }
    inline void Native(Engine::DirectX12Renderer& renderer)
    {
        using namespace SceneRuntime;
        std::string error;
        Require(ScriptModule::Reload(Root(),error),"native module loads with host ABI");
        Require(ScriptRegistry::Definitions().contains("ValidationNativeMover"),"native behaviour reaches host registry");
        const auto old=ScriptRegistry::Definitions().at("ValidationNativeMover");
        ScenePlacement object; object.id="native"; object.name="Native";
        object.scripts.push_back({"script",true,"ValidationNativeMover",{{"speed",8.0f}}});
        SceneLayout scene; scene.objects={object}; Editor::GameSession session;
        Require(session.Play(renderer,std::filesystem::absolute("Content"),scene,error),"native behaviour starts in common App and Editor runtime");
        session.Update(0.1,true);
        Require(std::abs(session.Runtime()->World().Layout().objects[0].position[0]-0.8f)<1e-5,"native callback reads host context parameters and changes live scene");
        Require(session.Stop(),"native script runtime stops before reload");
        Require(ScriptModule::Reload(Root(),error),"native module can be reloaded without original DLL lock");
        std::map<std::string,float> parameters,state; ScriptContext context{object,parameters,state,0.1}; context.data=&old.dataFields;
        old.update(context);
        Require(std::abs(object.position[0]-0.2f)<1e-5,"retained callback remains executable after module replacement");
        const auto folder=Root()/"Assets/Scripts/Bin"/ScriptModule::Configuration();
        const auto manifestPath=folder/"module.json";
        std::ifstream input(manifestPath); const std::string original(std::istreambuf_iterator<char>(input),{}); input.close();
        const auto manifest=Engine::Json::parse(original);
        const auto binary=folder/Engine::AssetDatabase::Path(manifest.at("file").get<std::string>());
        const auto handle=LoadLibraryExW(binary.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        Require(handle!=nullptr,"native ABI validation module opens");
        const auto entry=reinterpret_cast<RegisterScriptModule>(GetProcAddress(handle,"Wp1RegisterScripts"));
        ScriptModuleAbi incompatible; incompatible.version+=1;
        const bool rejected=entry && !entry(&incompatible,nullptr,RejectUnexpectedRegistration); FreeLibrary(handle);
        Require(rejected,"native module rejects incompatible host ABI");
        auto bad=manifest; bad["file"]="GameScripts-../escape.dll";
        { std::ofstream output(manifestPath,std::ios::trunc); output<<bad.dump(); }
        const bool failed=!ScriptModule::Reload(Root(),error);
        { std::ofstream output(manifestPath,std::ios::trunc); output<<original; }
        Require(failed && ScriptRegistry::Definitions().contains("ValidationNativeMover"),"failed native reload preserves previous behaviour and rejects path escape");
        Require(ScriptRegistry::InstallModule(Engine::AssetDatabase::Text(std::filesystem::weakly_canonical(Root())),{},error),"native test module unloads from registry");
    }
}
