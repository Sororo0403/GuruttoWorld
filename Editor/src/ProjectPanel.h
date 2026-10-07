#pragma once
#include "MaterialPanel.h"
#include "AssetManagementPanel.h"
#include "EditState.h"
#include "ProjectCatalog.h"
#include "AssetInfo.h"
#include "AssetPreview.h"
#include <imgui.h>

namespace Editor
{
    class ProjectPanel final
    {
    public:
        bool PreparePreview(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
        { return preview_.Prepare(renderer,root); }
        bool RenderPreview(ID3D12GraphicsCommandList* commands) { return preview_.Render(commands); }
        bool TakeAssetReloadRequest();
        void SetReloadPending(bool pending, std::string error) { reloadPending_=pending; watchError_=std::move(error); }
        void InvalidatePreview() { preview_.Invalidate(); previewed_.clear(); info_.reset(); }
        void Scan(const std::filesystem::path& root);
        void Draw(EditState& state, const std::array<float,3>& suggestedPosition, bool enabled);
        void DrawInspector(EditState& state, bool enabled=true);
        void RequestDrop(EditState& state, const std::string& path, const std::array<float,3>& position) const;
        const ProjectCatalog& Catalog() const { return catalog_; }
        std::optional<std::filesystem::path> TakeSceneRequest()
        {
            auto request=std::move(sceneRequest_);
            sceneRequest_.reset();
            return request;
        }
    private:
        static void DrawAssetInfo(const AssetInfo& info);
        void DrawFolder(const std::filesystem::path& folder);
        void DrawAssets(EditState& state, bool enabled);
        void DrawSelection(EditState& state, const std::array<float,3>& suggestedPosition, bool enabled);
        std::filesystem::path root_, folder_="Assets", selected_;
        MaterialPanel materialPanel_;
        AssetManagementPanel assetManagement_;
        ProjectCatalog catalog_;
        AssetPreview preview_;
        std::filesystem::path previewed_;
        std::optional<AssetInfo> info_;
        std::optional<std::filesystem::path> sceneRequest_;
        std::array<char,256> search_{};
        std::array<float,3> addPosition_{};
        bool positionInitialized_=false;
        int type_=0;
        std::string watchError_;
        bool reloadAssets_=false, reloadPending_=false;
    };
}
