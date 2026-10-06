#pragma once
#include "EditState.h"
#include "ProjectCatalog.h"
#include <imgui.h>

namespace Editor
{
    class ProjectPanel final
    {
    public:
        void Scan(const std::filesystem::path& root);
        void Draw(EditState& state, const std::array<float,3>& suggestedPosition, bool enabled);
        void RequestDrop(EditState& state, const std::string& path, const std::array<float,3>& position) const;
        const ProjectCatalog& Catalog() const { return catalog_; }
        std::optional<std::filesystem::path> TakeSceneRequest()
        {
            auto request=std::move(sceneRequest_);
            sceneRequest_.reset();
            return request;
        }
    private:
        void DrawFolder(const std::filesystem::path& folder);
        void DrawAssets(bool enabled);
        void DrawSelection(EditState& state, const std::array<float,3>& suggestedPosition, bool enabled);
        std::filesystem::path root_, folder_="Assets", selected_;
        ProjectCatalog catalog_;
        std::optional<std::filesystem::path> sceneRequest_;
        std::array<char,256> search_{};
        std::array<float,3> addPosition_{};
        bool positionInitialized_=false;
        int type_=0;
    };
}
