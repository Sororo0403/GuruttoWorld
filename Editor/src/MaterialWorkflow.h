#pragma once
#include "AssetFileTransaction.h"
#include "ProjectCatalog.h"
#include <SceneRuntime/MaterialAsset.h>
#include <SceneRuntime/SceneLayout.h>

namespace Editor {
struct MaterialWorkflow {
    static std::filesystem::path Target(const std::string& name) {
        if(name.empty() || name.find_first_of("/\\:<>|?*.\r\n\"")!=std::string::npos || name.back()==' ')
            throw std::runtime_error("Material名には予約文字を含まないファイル名を指定してください");
        const auto path=std::filesystem::path("Assets/Materials")/std::filesystem::path(std::u8string(name.begin(),name.end())+u8".mat");
        if(!SceneRuntime::MaterialAsset::ValidPath(path)) throw std::runtime_error("Materialの保存先が不正です");
        return path;
    }
    static void Duplicate(const std::filesystem::path& root,const SceneRuntime::MaterialAsset& asset,const std::filesystem::path& destination) {
        if(!SceneRuntime::MaterialAsset::ValidPath(destination)) throw std::runtime_error("Materialの保存先が不正です");
        auto metadata=root/destination;metadata+=".meta";
        if(std::filesystem::exists(root/destination) || std::filesystem::exists(metadata)) throw std::runtime_error("同じ名前のMaterialまたはメタデータが存在します");
        std::string error;
        if(!AssetFileTransaction::Run(root/destination,error,[&]() {
            asset.Save(root,destination);Engine::AssetDatabase::Ensure(root/destination);return true;
        })) throw std::runtime_error(error);
    }
    static std::vector<SceneRuntime::ScenePlacement> Assign(const SceneRuntime::SceneLayout& layout,const std::vector<std::string>& ids,const std::filesystem::path& path) {
        if(!SceneRuntime::MaterialAsset::ValidPath(path)) throw std::runtime_error("Materialの割り当て先が不正です");
        std::vector<SceneRuntime::ScenePlacement> result;
        for(const auto& object:layout.objects) if(std::find(ids.begin(),ids.end(),object.id)!=ids.end()) {
            auto candidate=object;
            if(!candidate.material) {
                std::string id="material";int counter=2;
                while(candidate.HasComponentId(id)) id="material-"+std::to_string(counter++);
                candidate.material.emplace();candidate.material->id=id;
            }
            candidate.material->asset=path;candidate.material->enabled=true;result.push_back(std::move(candidate));
        }
        if(result.size()!=ids.size()) throw std::runtime_error("割り当て対象のオブジェクトが見つかりません");
        return result;
    }
};
}
