#pragma once
#include "../Editor/src/ScriptAuthoringPanel.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace ScriptAuthoringValidation
{
    inline std::string Read(const std::filesystem::path& path)
    {
        std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};
    }
    inline void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    struct Fixture final
    {
        std::filesystem::path root=std::filesystem::absolute("Content"),source=root/"Assets/Scripts/ValidationAuthoringWatcher.cpp";
        std::filesystem::path manifest=root/"Assets/Scripts/Bin"/SceneRuntime::ScriptModule::Configuration()/"module.json";
        std::string original=Read(manifest);
        bool existed=std::filesystem::exists(manifest),created=false;
        ~Fixture()
        {
            try
            {
                if(created) std::filesystem::remove(source);
                if(existed) {std::ofstream output(manifest,std::ios::binary);output<<original;}
                else std::filesystem::remove(manifest);
                std::string ignored;SceneRuntime::ScriptModule::Reload(root,ignored);
            }
            catch(...) {}
        }
    };
    inline void Until(const std::function<bool()>& condition)
    {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
        while(std::chrono::steady_clock::now()<deadline) {if(condition()) return;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        throw std::runtime_error("Automatic script compilation/reload timed out; inspect generated/script-builds diagnostics");
    }
    inline void Run()
    {
        Fixture fixture;Editor::ScriptAuthoringPanel panel;std::string error;std::filesystem::path source;
        Require(SceneRuntime::ScriptModule::CreateSource(fixture.root,"ValidationAuthoringWatcher",source,error),"watcher creates a new source without overwriting");fixture.created=true;
        panel.Poll(fixture.root,false);Require(!panel.Ready(),"uncompiled source blocks Play");
        Until([&]{panel.Poll(fixture.root,false);return Read(fixture.manifest)!=fixture.original;});
        Require(!panel.Ready() && !SceneRuntime::ScriptRegistry::Definitions().contains("ValidationAuthoringWatcher"),"successful compilation defers module installation during Play");
        Until([&]{panel.Poll(fixture.root,true);return panel.Ready();});
        Require(SceneRuntime::ScriptRegistry::Definitions().contains("ValidationAuthoringWatcher"),"Stop installs the watched script into the real registry");
        const auto compiled=Read(fixture.manifest);{std::ofstream output(source,std::ios::app);output<<"\n// automatic watcher revision\n";}
        Until([&]{panel.Poll(fixture.root,false);return Read(fixture.manifest)!=compiled;});
        Require(!panel.Ready() && SceneRuntime::ScriptRegistry::Definitions().contains("ValidationAuthoringWatcher"),"editing recompiles while preserving the active definition until Stop");
        Until([&]{panel.Poll(fixture.root,true);return panel.Ready();});
    }
}
