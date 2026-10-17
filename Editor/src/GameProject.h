#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <SceneRuntime/ProjectSettings.h>
#include <algorithm>
#include <array>
#include <commdlg.h>
#include <shellapi.h>
#pragma comment(lib,"Comdlg32.lib")
#pragma comment(lib,"Shell32.lib")

namespace Editor
{
    struct GameProject final
    {
        /// <summary>外部ゲームプロジェクトを検証し、コンテンツのルートを取得します。</summary>
        static std::filesystem::path Open(std::filesystem::path path)
        {
            path=std::filesystem::absolute(path).lexically_normal();
            if(path.filename()=="WP1.project.json") path=path.parent_path();
            std::ifstream input(path/"WP1.project.json");
            if(!input) throw std::runtime_error("WP1.project.json is missing.");
            const auto descriptor=Engine::Json::parse(input);
            if(!descriptor.at("version").is_number_integer() || descriptor.at("version").get<int>()!=1) throw std::runtime_error("Unsupported game project version.");
            const auto name=descriptor.at("name").get<std::string>();
            if(name.empty() || name.size()>64 || !((name[0]>='A' && name[0]<='Z') || (name[0]>='a' && name[0]<='z')) || !std::all_of(name.begin(),name.end(),[](char c){return (c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_';}) ||
                descriptor.at("project").get<std::string>()!=name+".vcxproj" || !std::filesystem::is_regular_file(path/(name+".vcxproj")))
                throw std::runtime_error("Invalid game project descriptor.");
            auto engine=Engine::AssetDatabase::Path(descriptor.at("engineRoot").get<std::string>());
            if(engine.is_relative()) engine=(path/engine).lexically_normal();
            if(!std::filesystem::is_regular_file(engine/"scripts/BuildPlayer.ps1"))
                throw std::runtime_error("Game project engine checkout is missing.");
            if(!std::filesystem::is_regular_file(path/"Assets/Project.json") || !std::filesystem::is_directory(Engine::ContentFiles::Resolve(path/"Shaders/Mesh.hlsl").parent_path()))
                throw std::runtime_error("Game project content is missing.");
            SceneRuntime::ProjectSettings::Load(path);
            return path;
        }

        /// <summary>指定したプロジェクトを別のエディターウィンドウで開きます。</summary>
        static void BrowseAndOpen()
        {
            std::array<wchar_t,32768> file{};
            OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog);
            dialog.lpstrFilter=L"WP1 project\0WP1.project.json\0\0";
            dialog.lpstrFile=file.data(); dialog.nMaxFile=static_cast<DWORD>(file.size());
            dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(!GetOpenFileNameW(&dialog)) {
                if(CommDlgExtendedError()) throw std::runtime_error("Cannot open project file dialog.");
                return;
            }
            const auto root=Open(file.data());
            std::array<wchar_t,32768> executable{};
            const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
            if(!length || length>=executable.size()) throw std::runtime_error("Cannot locate Editor.exe.");
            const auto arguments=L"--project \""+root.wstring()+L"\"";
            if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",executable.data(),arguments.c_str(),nullptr,SW_SHOWNORMAL))<=32)
                throw std::runtime_error("Cannot open game project.");
        }
    };
}
