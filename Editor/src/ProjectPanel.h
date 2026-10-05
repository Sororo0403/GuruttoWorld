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
    private:
        void DrawFolder(const std::filesystem::path& folder);
        void DrawAssets();
        void DrawSelection(EditState& state, const std::array<float,3>& suggestedPosition, bool enabled);
        std::filesystem::path root_, folder_="Assets", selected_;
        ProjectCatalog catalog_;
        std::array<char,256> search_{};
        std::array<float,3> addPosition_{};
        bool positionInitialized_=false;
        int type_=0;
    };
}
