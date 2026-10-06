#pragma once
#include <imgui_internal.h>
#include <cstdio>
#include <string>

namespace Editor::Language
{
    inline void Initialize()
    {
        static constexpr ImGuiLocEntry entries[]{
            {ImGuiLocKey_TableSizeOne,"列幅を内容に合わせる###SizeOne"},
            {ImGuiLocKey_TableSizeAllFit,"すべての列幅を内容に合わせる###SizeAll"},
            {ImGuiLocKey_TableSizeAllDefault,"すべての列幅を初期値に戻す###SizeAll"},
            {ImGuiLocKey_TableReset,"リセット"},
            {ImGuiLocKey_TableResetOrder,"順序をリセット###ResetOrder"},
            {ImGuiLocKey_TableResetVisibility,"表示をリセット###ResetVisibility"},
            {ImGuiLocKey_WindowingMainMenuBar,"（メインメニュー）"},
            {ImGuiLocKey_WindowingPopup,"（ポップアップ）"},
            {ImGuiLocKey_WindowingUntitled,"（無題）"},
            {ImGuiLocKey_OpenLink_s,"「%s」を開く"},
            {ImGuiLocKey_CopyLink,"リンクをコピー###CopyLink"},
            {ImGuiLocKey_DockingHideTabBar,"タブバーを隠す###HideTabBar"},
            {ImGuiLocKey_DockingHoldShiftToDock,"Shiftを押したままパネルをドッキングできます。"},
            {ImGuiLocKey_DockingDragToUndockOrMoveNode,"ドラッグしてパネルグループを移動・切り離します。"},
        };
        ImGui::LocalizeRegisterEntries(entries,IM_ARRAYSIZE(entries));
    }

    inline void Replace(std::string& text,const std::string& from,const std::string& to)
    {
        size_t position=0;
        while((position=text.find(from,position))!=std::string::npos)
        {
            text.replace(position,from.size(),to);
            position+=to.size();
        }
    }

    // Plain legacy names hashed differently from ### names. Migrate window sections
    // and selected tab IDs while preserving dock nodes, positions and sizes.
    inline std::string MigrateLayout(std::string text)
    {
        for(const char* name:{"Street Editor","Inspector","Scene","Debug Camera","Console"})
        {
            const std::string stable="###"+std::string(name);
            Replace(text,"[Window]["+std::string(name)+"]","[Window]["+stable+"]");
            char oldId[32]{},newId[32]{};
            std::snprintf(oldId,sizeof(oldId),"Selected=0x%08X",ImHashStr(name));
            std::snprintf(newId,sizeof(newId),"Selected=0x%08X",ImHashStr(stable.c_str()));
            Replace(text,oldId,newId);
        }
        return text;
    }
}
