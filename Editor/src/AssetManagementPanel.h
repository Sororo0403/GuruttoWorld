#pragma once
#include "ProjectCatalog.h"
#include "AssetDependencies.h"
#include <Engine/Assets/AssetDatabase.h>
#include <imgui.h>
namespace Editor {
class AssetManagementPanel final {
    std::filesystem::path selected_;
    Engine::AssetMetadata settings_;
    std::array<char,1024> destination_{};
    std::string error_;
    AssetDependencies dependencies_;
public:
    std::optional<std::filesystem::path> Draw(const std::filesystem::path& root,const ProjectAsset& asset,bool enabled) {
        std::optional<std::filesystem::path> moved;
        bool disabled=false;
        try {
            if (selected_!=asset.path) {
                auto settings=Engine::AssetDatabase::Ensure(root/asset.path);
                settings_=std::move(settings); selected_=asset.path;
                destination_.fill(0); const auto text=ProjectCatalog::Text(selected_);
                std::copy_n(text.begin(),std::min(text.size(),destination_.size()-1),destination_.begin()); error_.clear();
            }
            ImGui::TextWrapped("アセットID: %s",settings_.id.c_str());
            ImGui::BeginDisabled(!enabled); disabled=true;
            if (asset.kind==AssetKind::Model && ImGui::CollapsingHeader("インポート設定###Import settings")) {
                ImGui::DragFloat("倍率###Scale",&settings_.scale,0.01f,0.0001f,10000,"%.4f",ImGuiSliderFlags_AlwaysClamp);
                ImGui::Checkbox("V座標を反転###Flip V",&settings_.flipV);
                if (ImGui::Button("設定を保存・再インポート###Save import")) Engine::AssetDatabase::Write(root/selected_,settings_);
            }
            if (asset.kind==AssetKind::Texture && ImGui::CollapsingHeader("画像インポート###Texture import")) {
                ImGui::Checkbox("Mipを生成###Mipmaps",&settings_.texture.mipmaps);
                ImGui::Checkbox("sRGBでMipを計算###sRGB mip filter",&settings_.texture.srgb);
                int size=static_cast<int>(settings_.texture.maxSize);
                if (ImGui::DragInt("最大サイズ###Max texture size",&size,1,1,16384,"%d",ImGuiSliderFlags_AlwaysClamp)) settings_.texture.maxSize=static_cast<unsigned int>(size);
                if (ImGui::BeginCombo("圧縮###Compression",settings_.texture.compression.c_str())) {
                    for(const auto* mode:{"none","bc3"}) if(ImGui::Selectable(mode,settings_.texture.compression==mode)) settings_.texture.compression=mode;
                    ImGui::EndCombo();
                }
                if(settings_.texture.compression=="bc3") ImGui::TextWrapped("縦横が4の倍数の画像を圧縮します。それ以外は元の寸法で読み込みます。");
                if (ImGui::Button("設定を保存・再インポート###Save texture import")) Engine::AssetDatabase::Write(root/selected_,settings_);
            }
            if (ImGui::CollapsingHeader("移動・改名###Move asset")) {
                ImGui::InputText("移動先###Destination",destination_.data(),destination_.size());
                ImGui::TextUnformatted("同じ種類のAssetsフォルダー内で移動できます。IDを維持します。");
                if (ImGui::Button("移動・改名を実行###Move")) {
                    const auto target=Engine::AssetDatabase::Path(destination_.data());
                    Engine::AssetDatabase(root).Move(selected_,target); moved=target; selected_.clear(); error_.clear();
                }
            }
            ImGui::EndDisabled(); disabled=false;
            if (ImGui::CollapsingHeader("参照するアセット###Dependencies")) {
                dependencies_.Refresh(root,asset);
                for (const auto& path:dependencies_.Values()) ImGui::BulletText("%s",path.c_str());
                if (!dependencies_.Error().empty()) ImGui::TextWrapped("%s",dependencies_.Error().c_str());
                else if (dependencies_.Values().empty()) ImGui::TextUnformatted("参照はありません。");
            }
        } catch (const std::exception& error) { error_=error.what(); }
        if (disabled) ImGui::EndDisabled();
        if (!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
        return moved;
    }
};
}
