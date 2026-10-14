#pragma once
#include "../Editor/src/SceneDocument.h"
#include "EnvironmentValidation.h"
#include <SceneRuntime/Prefab.h>
#include <SceneRuntime/GamePlayback.h>
#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <algorithm>
#include <cstdint>
#include <fstream>

namespace EngineEditorReviewValidation
{
    /// <summary>回帰条件を満たさない場合に検証を終了します。</summary>
    inline void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    /// <summary>検証プロセスごとに独立した生成物の保存先を取得します。</summary>
    inline const std::filesystem::path& Fixtures()
    {
        static const auto root=std::filesystem::absolute("generated/tests/engine-editor-review")/
            (std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        return root;
    }
    /// <summary>不正な操作が例外として拒否されることを確認します。</summary>
    template<class Action> inline bool Rejected(Action action)
    {
        try {action();return false;} catch(const std::exception&) {return true;}
    }
    /// <summary>編集データの再発条件を検証します。</summary>
    inline void Data()
    {
        const auto& root=Fixtures();std::filesystem::create_directories(root/"Assets/Scenes");
        const auto source=root/"Assets/Scenes/A.json",destination=root/"Assets/Scenes/B.json";
        SceneRuntime::SceneLayout initial,external;SceneRuntime::ScenePlacement item;item.id="external";external.objects={item};
        initial.Save(source);Editor::SceneDocument document(source);external.Save(source);
        Require(Rejected([&] {document.Save(initial);}),"external edits prevent saving before rename");
        Engine::AssetDatabase(root).Move("Assets/Scenes/A.json","Assets/Scenes/B.json");document.AssetMoved(source,destination);
        Require(Rejected([&] {document.Save(initial);}) && SceneRuntime::SceneLayout::Load(destination).Serialize()==external.Serialize(),
            "rename must preserve conflict baseline and external file contents");
        initial.Save(source);Editor::SceneDocument normal(source);
        Engine::AssetDatabase(root).Move("Assets/Scenes/A.json","Assets/Scenes/C.json");normal.AssetMoved(source,root/"Assets/Scenes/C.json");
        normal.Save(external);Require(SceneRuntime::SceneLayout::Load(normal.Path()).objects.size()==1,"uncontested renamed document still saves");

    }
    /// <summary>追加済みの実行時の回帰条件を検証します。</summary>
    inline void Runtime(Engine::DirectX12Renderer&,const std::filesystem::path&) {}
}
