#pragma once
#include "ProjectCatalog.h"
#include <SceneRuntime/MaterialAsset.h>
#include <imgui.h>

namespace Editor
{
    class MaterialPanel final
    {
    public:
        void Draw(const std::filesystem::path& root,const std::filesystem::path& path,const ProjectCatalog& catalog,bool enabled)
        {
            if (path_!=path)
            {
                path_=path;
                try { asset_=SceneRuntime::MaterialAsset::Load(root,path); error_.clear(); }
                catch (const std::exception& exception) { error_=exception.what(); }
            }
            ImGui::BeginDisabled(!enabled);
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
                try { asset_.Save(root,path); error_.clear(); }
                catch (const std::exception& exception) { error_=exception.what(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("再読み込み###Reload material"))
            {
                try { asset_=SceneRuntime::MaterialAsset::Load(root,path); error_.clear(); }
                catch (const std::exception& exception) { error_=exception.what(); }
            }
            ImGui::EndDisabled();
            if (!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
        }
    private:
        std::filesystem::path path_;
        SceneRuntime::MaterialAsset asset_;
        std::string error_;
    };
}
