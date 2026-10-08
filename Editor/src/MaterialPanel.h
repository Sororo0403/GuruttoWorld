#pragma once
#include "ProjectCatalog.h"
#include "MaterialDocument.h"
#include <map>
#include <SceneRuntime/MaterialAsset.h>
#include <imgui.h>

namespace Editor
{
    class MaterialPanel final
    {
    public:
        bool HasChanges() const
        { return std::any_of(documents_.begin(),documents_.end(),[](const auto& item) { return item.second.Dirty(); }); }
        SceneRuntime::MaterialAsset Draft(const std::filesystem::path& root,const std::filesystem::path& path) {
            auto& document=documents_[path];document.Refresh(root,path,true);
            if(!document.Loaded()) throw std::runtime_error("Materialを読み込めません");
            return document.Asset();
        }
        void SaveAll(const std::filesystem::path& root)
        { for (auto& [path,document] : documents_) if (document.Dirty()) document.Save(root,path); }
        void AssetMoved(const std::filesystem::path& source,const std::filesystem::path& destination)
        {
            auto entry=documents_.extract(source);
            if (!entry.empty()) { entry.key()=destination; documents_.insert(std::move(entry)); }
        }
        void Draw(const std::filesystem::path& root,const std::filesystem::path& path,const ProjectCatalog& catalog,bool enabled)
        {
            auto& document=documents_[path];
            try { document.Refresh(root,path); error_.clear(); }
            catch (const std::exception& exception) { error_=exception.what(); }
            auto& asset_=document.Asset();
            if (document.Dirty()) ImGui::TextUnformatted("Materialに未保存の変更があります。");
            if (document.Conflict()) ImGui::TextWrapped("外部の変更と競合しています。再読み込みでは編集中の値を破棄します。");
            ImGui::BeginDisabled(!enabled || !document.Loaded() || !error_.empty());
            ImGui::ColorEdit4("色・透明度###Material color",asset_.values.color.data());
            ImGui::SliderFloat("粗さ###Roughness",&asset_.values.roughness,0.04f,1,"%.2f");
            ImGui::SliderFloat("金属感###Metallic",&asset_.values.metallic,0,1,"%.2f");
            ImGui::Checkbox("物理ベースの照明（PBR）###Physically based",&asset_.values.physicallyBased);
            ImGui::Checkbox("透明として描画###Transparent",&asset_.values.transparent);
            const auto label=asset_.texture.empty() ? std::string("モデルの画像を使用") : ProjectCatalog::Text(asset_.texture.filename());
            if (ImGui::BeginCombo("画像###Albedo texture",label.c_str()))
            {
                if (ImGui::Selectable("モデルの画像を使用",asset_.texture.empty())) asset_.texture.clear();
                for (const auto& image : catalog.Assets()) if (image.kind==AssetKind::Texture && ProjectCatalog::Text(image.path).starts_with("Assets/Textures/"))
                    if (ImGui::Selectable(ProjectCatalog::Text(image.path).c_str(),image.path==asset_.texture)) asset_.texture=image.path;
                ImGui::EndCombo();
            }
            const auto normalLabel=asset_.normalTexture.empty() ? std::string("法線マップなし") : ProjectCatalog::Text(asset_.normalTexture.filename());
            if (ImGui::BeginCombo("法線マップ###Normal texture",normalLabel.c_str()))
            {
                if (ImGui::Selectable("法線マップなし",asset_.normalTexture.empty())) asset_.normalTexture.clear();
                for (const auto& image : catalog.Assets()) if (image.kind==AssetKind::Texture && ProjectCatalog::Text(image.path).starts_with("Assets/Textures/"))
                    if (ImGui::Selectable(ProjectCatalog::Text(image.path).c_str(),image.path==asset_.normalTexture)) asset_.normalTexture=image.path;
                ImGui::EndCombo();
            }
            ImGui::Checkbox("法線マップのYを反転###Normal flip Y",&asset_.values.normalFlipY);
            ImGui::DragFloat2("画像の拡縮###UV scale",asset_.values.uv.scale.data(),0.05f,-100000,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            float degrees=asset_.values.uv.rotation*57.2957795f;
            if (ImGui::DragFloat("画像の回転（度）###UV rotation",&degrees,1,-36000,36000,"%.1f",ImGuiSliderFlags_AlwaysClamp)) asset_.values.uv.rotation=degrees*0.0174532925f;
            ImGui::DragFloat2("画像の移動###UV translation",asset_.values.uv.translation.data(),0.05f,-100000,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::Button("Materialを保存###Save material"))
            {
                try { document.Save(root,path); error_.clear(); }
                catch (const std::exception& exception) { error_=exception.what(); }
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!enabled);
            ImGui::SameLine();
            if (ImGui::Button("再読み込み###Reload material"))
            {
                if (document.Dirty()) ImGui::OpenPopup("Materialの編集を破棄###Discard material draft");
                else Reload(document,root,path);
            }
            if (ImGui::BeginPopupModal("Materialの編集を破棄###Discard material draft",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("未保存のMaterial編集を破棄して読み込み直しますか？");
                if (ImGui::Button("破棄して再読み込み###Discard and reload material"))
                { Reload(document,root,path); ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル###Cancel material reload")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::EndDisabled();
            if (!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
        }
    private:
        void Reload(MaterialDocument& document,const std::filesystem::path& root,const std::filesystem::path& path)
        {
            try { document.Reload(root,path); error_.clear(); }
            catch (const std::exception& exception) { error_=exception.what(); }
        }
        std::map<std::filesystem::path,MaterialDocument> documents_;
        std::string error_;
    };
}
