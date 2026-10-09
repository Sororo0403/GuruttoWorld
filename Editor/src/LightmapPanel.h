#pragma once
#include "EditState.h"
#include <SceneRuntime/LightmapBaker.h>
#include <SceneRuntime/MaterialAsset.h>
#include <imgui.h>

namespace Editor
{
    class LightmapPanel final
    {
    public:
        /// <summary>選択した静的メッシュへ照明をベイクし、Undo可能なMaterial割り当てを予約します。</summary>
        void Draw(const SceneRuntime::SceneWorld& world,EditState& state,const std::filesystem::path& root,bool enabled)
        {
            if (!ImGui::Begin("照明ベイク###Lighting bake")) { ImGui::End(); return; }
            const auto& objects=world.Layout().objects;
            const auto selected=std::find_if(objects.begin(),objects.end(),[&](const auto& object) { return object.id==state.SelectedId(); });
            const bool valid=selected!=objects.end() && selected->meshRenderer && !selected->animator && state.SingleSelection();
            ImGui::TextWrapped("静的メッシュを一つ選択してください。元UVは0〜1の範囲で重ならないように用意してください。現在の光源と実形状の遮蔽を保存します。");
            ImGui::TextWrapped("ライトマップを割り当てたMaterialはベイクした照度を使います。光源や形状を編集したら再ベイクしてください。照度はLDRに保存します。");
            ImGui::SliderInt("解像度###Bake resolution",&resolution_,16,512);
            ImGui::BeginDisabled(!enabled || !valid);
            if (ImGui::Button("選択したメッシュをベイク###Bake selected mesh"))
            {
                try { Bake(world,state,root,*selected); error_.clear(); }
                catch (const std::exception& exception) { error_=exception.what(); }
            }
            ImGui::EndDisabled();
            if (!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
            if (!result_.empty()) ImGui::TextWrapped("生成した照明：%s",result_.c_str());
            ImGui::End();
        }
    private:
        /// <summary>画像と独立したMaterialを保存し、既存資産を変更せずにシーン編集を要求します。</summary>
        void Bake(const SceneRuntime::SceneWorld& world,EditState& state,const std::filesystem::path& root,const SceneRuntime::ScenePlacement& placement)
        {
            const auto pixels=SceneRuntime::LightmapBaker::Bake(world,root,placement.id,static_cast<UINT>(resolution_));
            const auto key=std::to_string(GetTickCount64())+"-"+std::to_string(serial_++);
            const std::filesystem::path image="Assets/Textures/Baked/Lightmap-"+key+".bmp";
            const std::filesystem::path material="Assets/Materials/Baked/Material-"+key+".mat";
            SceneRuntime::LightmapBaker::Save(root,image,static_cast<UINT>(resolution_),pixels);
            auto asset=placement.Material().empty() ? SceneRuntime::MaterialAsset{} : SceneRuntime::MaterialAsset::Load(root,placement.Material());
            asset.lightmap=image; asset.Save(root,material);
            auto candidate=placement;
            if (!candidate.material)
            {
                std::string id="material"; size_t counter=2;
                while (candidate.HasComponentId(id)) id="material-"+std::to_string(counter++);
                candidate.material.emplace(); candidate.material->id=id;
            }
            candidate.material->enabled=true; candidate.material->asset=material;
            for (size_t index=0;index<candidate.material->slots.size();++index)
                if (!candidate.material->slots[index].empty())
                {
                    auto slot=SceneRuntime::MaterialAsset::Load(root,candidate.material->slots[index]); slot.lightmap=image;
                    const std::filesystem::path target="Assets/Materials/Baked/Material-"+key+"-"+std::to_string(index)+".mat";
                    slot.Save(root,target); candidate.material->slots[index]=target;
                }
            ObjectRequest request; request.action=ObjectAction::Components; request.id=placement.id; request.components=std::move(candidate);
            state.Request(std::move(request)); result_=image.generic_string();
        }
        int resolution_=128;
        UINT64 serial_=0;
        std::string error_,result_;
    };
}
