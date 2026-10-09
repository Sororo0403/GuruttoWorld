#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <map>
namespace Engine { class DirectX12Renderer; class Camera; }
namespace SceneRuntime {
class SceneWorld;
struct UiState {
    std::map<std::string,float> values;
    std::map<std::string,bool> visibility;
    std::string hovered, pressed;
    std::map<std::string,std::string> strings;
    std::map<std::string,std::array<float,2>> scrollOffsets;
    std::string focused;
    std::array<float,2> dragPosition{}, dragScroll{};
    float Value(const std::string& key, float fallback=0) const;
    bool Matches(const std::string& expression) const;
    bool Assign(const std::string& expression);
};
struct UiRect {
    std::array<float,2> position{},size{};
    float rotation=0, opacity=1, scale=1;
    bool visible=true;
    std::array<float,4> clip{-100000,-100000,100000,100000};
    bool Contains(float x,float y) const;
};
struct UiEvent { std::string object,action,target,sound,event; float value=0; std::string text; };
class SceneUi final {
public:
    bool Prepare(const Engine::DirectX12Renderer&, const std::filesystem::path&, const SceneLayout&, std::string& error,const UiState& state={});
    void Draw(ID3D12GraphicsCommandList*,const SceneLayout&,unsigned int width,unsigned int height,const UiState& state={}) const;
    void DrawScene(ID3D12GraphicsCommandList*,const SceneWorld&,const Engine::Camera&,const UiState& state={}) const;
    static UiState Defaults(const SceneLayout&);
    /// <summary>シーン切り替えの初期状態へ、保持したUIの実行値を引き継ぎます。</summary>
    static UiState SceneState(const SceneLayout& previousLayout,const SceneLayout& layout,const UiState& previous,bool reset);
    static UiRect Resolve(const SceneLayout&,const ScenePlacement&,unsigned int width,unsigned int height,const UiState& state={});
    static std::string Hit(const SceneLayout&,unsigned int width,unsigned int height,float x,float y,const UiState& state={},bool buttonsOnly=true);
    static UiEvent Activate(const SceneLayout&,const std::string& object,UiState& state);
    /// <summary>クリック開始・ドラッグ・解放・ホイールを共通UIの状態へ適用します。</summary>
    static UiEvent Pointer(const SceneLayout&,unsigned int width,unsigned int height,float x,float y,bool down,bool pressed,bool released,float wheel,UiState& state);
    /// <summary>UTF-32文字入力をフォーカス中の入力欄へ反映します。</summary>
    static UiEvent TextInput(const SceneLayout&,char32_t character,UiState& state);
    /// <summary>実行状態を反映した入力欄の表示文字列を取得します。</summary>
    static std::string InputText(const ScenePlacement&,const UiState&);
    static std::string Shortcut(const SceneLayout&,const std::string& key,unsigned int width,unsigned int height,const UiState& state);
private:
    struct Resource {std::string signature; std::shared_ptr<Engine::SpriteRenderer> sprite,sceneSprite;};
    void Prune(const Engine::DirectX12Renderer&,const SceneLayout&);
    void PreparePart(const Engine::DirectX12Renderer&,const std::filesystem::path&,const ScenePlacement&,bool text,std::map<std::string,Resource>& pending);
    void DrawPart(ID3D12GraphicsCommandList*,const ScenePlacement&,const UiRect&,unsigned int width,unsigned int height,const UiState&,bool text) const;
    std::map<std::string,Resource> resources_;
};
}
