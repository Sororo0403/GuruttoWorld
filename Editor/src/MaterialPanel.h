#pragma once
#include "ProjectCatalog.h"
#include "MaterialDocument.h"
#include <map>
#include <SceneRuntime/MaterialAsset.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace Editor
{
    class MaterialPanel final
    {
    public:
        bool HasChanges() const
        {
            return std::any_of(documents_.begin(), documents_.end(),
                               [](const auto& item)
                               {
                                   return item.second.Dirty();
                               });
        }
        bool CanUndo(const std::filesystem::path& path, bool redo) const
        {
            const auto found = documents_.find(path);
            return found != documents_.end() && (redo ? found->second.CanRedo() : found->second.CanUndo());
        }
        bool Undo(const std::filesystem::path& path, bool redo)
        {
            const auto found = documents_.find(path);
            return found != documents_.end() && found->second.Undo(redo);
        }
        SceneRuntime::MaterialAsset Draft(const std::filesystem::path& root, const std::filesystem::path& path)
        {
            auto& document = documents_[path];
            document.Refresh(root, path, true);
            if (!document.Loaded())
                throw std::runtime_error("Materialを読み込めません");
            return document.Asset();
        }
        /// <summary>未保存Materialを一つの取引で保存し、途中失敗時は先行する書き込みも戻します。</summary>
        void SaveAll(const std::filesystem::path& root)
        {
            std::vector<DocumentSavePlan> plans;
            PrepareSaves(root, plans);
            DocumentSave::Apply(std::move(plans));
        }
        /// <summary>未保存Materialをすべて検査し、ファイルへ書かずに一括保存へ追加します。</summary>
        void PrepareSaves(const std::filesystem::path& root, std::vector<DocumentSavePlan>& plans)
        {
            for (auto& [path, document] : documents_)
                if (document.Dirty())
                    plans.push_back(document.PrepareSave(root, path));
        }
        /// <summary>移動先に未保存のMaterial編集がある場合はファイル移動前に中止します。</summary>
        void ValidateMove(const std::filesystem::path& source, const std::filesystem::path& destination) const
        {
            const auto found = documents_.find(destination);
            if (source != destination && found != documents_.end() && found->second.Dirty())
                throw std::runtime_error(
                    "移動先のMaterialに未保存の変更があります。保存するか編集を破棄してから移動してください。");
        }
        /// <summary>古い移動先の読み込みデータを除去し、移動元の編集と履歴を引き継ぎます。</summary>
        void AssetMoved(const std::filesystem::path& source, const std::filesystem::path& destination)
        {
            if (source == destination)
                return;
            ValidateMove(source, destination);
            for (auto& [path, document] : documents_)
            {
                static_cast<void>(path);
                document.AssetMoved(source, destination);
            }
            documents_.erase(destination);
            auto entry = documents_.extract(source);
            if (!entry.empty())
            {
                entry.key() = destination;
                documents_.insert(std::move(entry));
            }
        }
        void Draw(const std::filesystem::path& root, const std::filesystem::path& path, const ProjectCatalog& catalog,
                  bool enabled)
        {
            auto& document = documents_[path];
            try
            {
                document.Refresh(root, path);
                error_.clear();
            }
            catch (const std::exception& exception)
            {
                error_ = exception.what();
            }
            auto& asset_ = document.Asset();
            if (document.Dirty())
                ImGui::TextUnformatted("Materialに未保存の変更があります。");
            if (document.Conflict())
                ImGui::TextWrapped("外部の変更と競合しています。再読み込みでは編集中の値を破棄します。");
            ImGui::BeginDisabled(!enabled || !document.Loaded() || !error_.empty());
            DrawShader(asset_, catalog);
            ImGui::TextWrapped(
                "シェーダーはMesh.hlslと同じ頂点入力・定数・テクスチャのインターフェースを使用してください。");
            ImGui::ColorEdit4("色・透明度###Material color", asset_.values.color.data());
            ImGui::SliderFloat("粗さ###Roughness", &asset_.values.roughness, 0.04f, 1, "%.2f");
            ImGui::SliderFloat("金属感###Metallic", &asset_.values.metallic, 0, 1, "%.2f");
            ImGui::DragFloat4("シェーダーパラメーター###Shader parameters", asset_.values.shaderParameters.data(), .01f,
                              -100000, 100000);
            ImGui::Checkbox("物理ベースの照明（PBR）###Physically based", &asset_.values.physicallyBased);
            ImGui::Checkbox("透明として描画###Transparent", &asset_.values.transparent);
            DrawTextures(asset_, catalog);
            ImGui::SliderFloat("環境照明の強度###Environment intensity", &asset_.values.environmentIntensity, 0, 100);
            ImGui::TextWrapped("環境照明は正距円筒パノラマを使います。ライトマップはモデルの元のUVへ適用します。");
            ImGui::DragFloat2("画像の拡縮###UV scale", asset_.values.uv.scale.data(), 0.05f, -100000, 100000, "%.2f",
                              ImGuiSliderFlags_AlwaysClamp);
            float degrees = asset_.values.uv.rotation * 57.2957795f;
            if (ImGui::DragFloat("画像の回転（度）###UV rotation", &degrees, 1, -36000, 36000, "%.1f",
                                 ImGuiSliderFlags_AlwaysClamp))
                asset_.values.uv.rotation = degrees * 0.0174532925f;
            ImGui::DragFloat2("画像の移動###UV translation", asset_.values.uv.translation.data(), 0.05f, -100000,
                              100000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            DrawDocumentActions(document, root, path);
            ImGui::EndDisabled();
            DrawReload(document, root, path, enabled);
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
        }

    private:
        void DrawShader(SceneRuntime::MaterialAsset& asset_, const ProjectCatalog& catalog)
        {
            if (ImGui::BeginCombo("メッシュシェーダー###Mesh shader",
                                  asset_.shader.empty() ? "標準" : ProjectCatalog::Text(asset_.shader).c_str()))
            {
                if (ImGui::Selectable("標準###Default", asset_.shader.empty()))
                    asset_.shader.clear();
                for (const auto& entry : catalog.Assets())
                    if (entry.kind == AssetKind::Shader && entry.path.extension() == ".hlsl")
                        if (ImGui::Selectable(ProjectCatalog::Text(entry.path).c_str(), asset_.shader == entry.path))
                            asset_.shader = entry.path;
                ImGui::EndCombo();
            }
        }

        static void ChooseTexture(const char* label, const char* emptyLabel, std::filesystem::path& texture,
                                  const ProjectCatalog& catalog)
        {
            const auto preview = texture.empty() ? std::string(emptyLabel) : ProjectCatalog::Text(texture.filename());
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                if (ImGui::Selectable(emptyLabel, texture.empty()))
                    texture.clear();
                for (const auto& image : catalog.Assets())
                    if (image.kind == AssetKind::Texture &&
                        ProjectCatalog::Text(image.path).starts_with("Assets/Textures/"))
                        if (ImGui::Selectable(ProjectCatalog::Text(image.path).c_str(), image.path == texture))
                            texture = image.path;
                ImGui::EndCombo();
            }
        }
        void DrawTextures(SceneRuntime::MaterialAsset& asset_, const ProjectCatalog& catalog)
        {
            ChooseTexture("画像###Albedo texture", "モデルの画像を使用", asset_.texture, catalog);
            ChooseTexture("法線マップ###Normal texture", "法線マップなし", asset_.normalTexture, catalog);
            ImGui::Checkbox("法線マップのYを反転###Normal flip Y", &asset_.values.normalFlipY);
            for (const auto& entry : {std::pair{"環境照明パノラマ###Environment panorama", &asset_.environmentTexture},
                                      std::pair{"ベイク済みライトマップ###Baked lightmap", &asset_.lightmap}})
            {
                ChooseTexture(entry.first, "なし", *entry.second, catalog);
            }
        }

        void DrawDocumentActions(MaterialDocument& document, const std::filesystem::path& root,
                                 const std::filesystem::path& path)
        {
            const auto active = ImGui::GetCurrentContext()->ActiveId;
            document.Observe(active ? "material/" + std::to_string(active) : std::string{});
            ImGui::BeginDisabled(!document.CanUndo());
            if (ImGui::Button("Materialの編集を元に戻す###Undo material draft"))
                document.Undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!document.CanRedo());
            if (ImGui::Button("やり直す###Redo material draft"))
                document.Undo(true);
            ImGui::EndDisabled();
            ImGui::TextWrapped("UndoはこのMaterialの編集値を戻します。保存済みファイルへの反映には保存が必要です。");
            if (ImGui::Button("Materialを保存###Save material"))
            {
                try
                {
                    document.Save(root, path);
                    error_.clear();
                }
                catch (const std::exception& exception)
                {
                    error_ = exception.what();
                }
            }
        }

        void DrawReload(MaterialDocument& document, const std::filesystem::path& root,
                        const std::filesystem::path& path, bool enabled)
        {
            ImGui::BeginDisabled(!enabled);
            ImGui::SameLine();
            if (ImGui::Button("再読み込み###Reload material"))
            {
                if (document.Dirty())
                    ImGui::OpenPopup("Materialの編集を破棄###Discard material draft");
                else
                    Reload(document, root, path);
            }
            if (ImGui::BeginPopupModal("Materialの編集を破棄###Discard material draft", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("未保存のMaterial編集を破棄して読み込み直しますか？");
                if (ImGui::Button("破棄して再読み込み###Discard and reload material"))
                {
                    Reload(document, root, path);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル###Cancel material reload"))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::EndDisabled();
        }

        void Reload(MaterialDocument& document, const std::filesystem::path& root, const std::filesystem::path& path)
        {
            try
            {
                document.Reload(root, path);
                error_.clear();
            }
            catch (const std::exception& exception)
            {
                error_ = exception.what();
            }
        }
        std::map<std::filesystem::path, MaterialDocument> documents_;
        std::string error_;
    };
}
