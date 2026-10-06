#pragma once
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include "../Editor/src/EditorFonts.h"
#include "../Editor/src/EditorLanguage.h"
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <imgui.h>
#include <array>
#include <cstring>
#include <cmath>
#include <stdexcept>
namespace EditorFontValidation {
inline void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
inline void Run(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root) {
    Editor::Language::Initialize();
    Require(std::string_view(ImGui::LocalizeGetMsg(ImGuiLocKey_DockingHideTabBar))=="タブバーを隠す###HideTabBar","docking context menu is Japanese");
    char selected[32]{};
    std::snprintf(selected,sizeof(selected),"Selected=0x%08X",ImHashStr("Scene"));
    const auto migrated=Editor::Language::MigrateLayout(std::string("[Window][Scene]\nPos=20,40\nSize=500,300\nDockId=0x00000005,0\n[Docking][Data]\nDockNode ID=0x00000005 ")+selected+"\n");
    std::snprintf(selected,sizeof(selected),"Selected=0x%08X",ImHashStr("シーン###Scene"));
    Require(migrated.find("[Window][###Scene]")!=std::string::npos && migrated.find(selected)!=std::string::npos && migrated.find("DockId=0x00000005,0")!=std::string::npos,"legacy scene dock placement and selected tab migrate");
    Require(Editor::Language::MigrateLayout(migrated)==migrated,"Japanese layout migration is idempotent");
    auto& io=ImGui::GetIO(); const int count=io.Fonts->Fonts.Size;
    std::string error;
    Require(!Editor::EditorFonts::Initialize(root/"missing-fonts",error) && !error.empty() && io.Fonts->Fonts.Size==count,"missing editor fonts fail before changing atlas");
    Require(Editor::EditorFonts::Initialize(root,error),"editor fonts load");
    auto* font=io.FontDefault;
    Require(font && std::strcmp(font->GetDebugName(),"Editor Fira Mono")==0 && font->Sources.Size==2,"Fira Mono and Japanese merge into the default font");
    const int loadedCount=io.Fonts->Fonts.Size;
    Require(Editor::EditorFonts::Initialize(root,error) && io.Fonts->Fonts.Size==loadedCount,"editor font initialization is idempotent");
    for(const ImWchar c:{ImWchar(0x3042),ImWchar(0x30a2),ImWchar(0x65e5),ImWchar(0x672c),ImWchar(0x8a9e),ImWchar(0x8907),ImWchar(0xff76)})
        Require(font->IsGlyphInFont(c),"Japanese glyph exists without fallback");
    auto* baked=font->GetFontBaked(16);
    const auto* narrow=baked->FindGlyphNoFallback('i'); const auto* wide=baked->FindGlyphNoFallback('W');
    Require(narrow && wide && std::abs(narrow->AdvanceX-wide->AdvanceX)<0.001f,"ASCII remains monospace after Japanese merge");
    for(const ImWchar c:{ImWchar(0x65e5),ImWchar(0x672c),ImWchar(0x8a9e)}) {
        const auto* glyph=baked->FindGlyphNoFallback(c);
        Require(glyph && glyph->Visible && glyph->X1>glyph->X0,"Japanese glyph rasterizes");
    }
    std::array<char,128> text{};
    const auto frame=[&](bool focus) {
        Require(renderer.Render({0,0,0,1},[](auto*,float){},[&] {
            ImGui::SetNextWindowPos({20,20}); ImGui::SetNextWindowSize({500,200});
            ImGui::Begin("Font validation");
            ImGui::TextUnformatted("Fira Mono 0123 / 日本語・ひらがな・カタカナ・複製");
            if(focus) ImGui::SetKeyboardFocusHere();
            ImGui::InputText("UTF-8 input",text.data(),text.size());
            ImGui::End();
        })!=Engine::RenderResult::Failed,"editor font frame renders");
    };
    frame(true); frame(false);
    io.AddInputCharactersUTF8("日本語"); frame(false);
    Require(std::string(text.data())=="日本語","Japanese text enters the UTF-8 input field");
    Require(renderer.WaitForIdle(),"dynamic editor font texture uploads complete");
}
}
#endif
