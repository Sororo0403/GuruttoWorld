#include "EditorFonts.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <exception>
namespace {
constexpr float FontSize=16.0f;
constexpr const char* FontName="Editor Fira Mono";
std::string Utf8(const std::filesystem::path& path) {
    const auto text=path.generic_u8string(); return {text.begin(),text.end()};
}
ImFont* Load(ImFontAtlas& atlas,const std::filesystem::path& path,ImFontConfig config,const char* name) {
    config.Flags|=ImFontFlags_NoLoadError;
    config.PixelSnapH=true;
    std::snprintf(config.Name,sizeof(config.Name),"%s",name);
    return atlas.AddFontFromFileTTF(Utf8(path).c_str(),FontSize,&config);
}
}
namespace Editor {
bool EditorFonts::Initialize(const std::filesystem::path& root,std::string& error) {
    try {
        if(!ImGui::GetCurrentContext()) {error="フォントの初期化にはImGuiコンテキストが必要です"; return false;}
        auto& io=ImGui::GetIO();
        if(io.FontDefault && std::strcmp(io.FontDefault->GetDebugName(),FontName)==0) {error.clear(); return true;}
        if(io.Fonts->Locked) {error="ImGuiフレームの開始前にフォントを初期化してください"; return false;}
        const auto latin=root/"Assets/Fonts/FiraMono/FiraMono-Regular.ttf";
        const auto japanese=root/"Assets/Fonts/MPlus1p/MPLUS1p-Regular.ttf";
        for(const auto& path:{latin,japanese}) if(!std::filesystem::is_regular_file(path)) {
            error="エディターのフォントがありません："+Utf8(path); return false;
        }
        auto* font=Load(*io.Fonts,latin,{},FontName);
        if(!font) {error="Fira Monoを読み込めません"; return false;}
        ImFontConfig merge; merge.MergeMode=true; merge.DstFont=font;
        static constexpr ImWchar ExcludeLatin[]={0x0020,0x007e,0};
        merge.GlyphExcludeRanges=ExcludeLatin;
        if(!Load(*io.Fonts,japanese,merge,"M PLUS 1p Japanese")) {error="日本語フォントを読み込めません"; return false;}
        io.FontDefault=font;
        ImGui::GetStyle().FontSizeBase=FontSize;
        error.clear(); return true;
    } catch(const std::exception& exception) {error=exception.what(); return false;}
}
}
