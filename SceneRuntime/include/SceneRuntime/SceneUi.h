#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <map>
namespace Engine { class DirectX12Renderer; }
namespace SceneRuntime {
struct UiState {
    std::map<std::string,float> values;
    std::map<std::string,bool> visibility;
    std::string hovered, pressed;
    float Value(const std::string& key, float fallback=0) const;
    bool Matches(const std::string& expression) const;
    bool Assign(const std::string& expression);
};
struct UiRect {
    std::array<float,2> position{},size{};
    float rotation=0, opacity=1, scale=1;
    bool visible=true;
    bool Contains(float x,float y) const;
};
struct UiEvent { std::string object,action,target,sound,event; };
class SceneUi final {
public:
    bool Prepare(const Engine::DirectX12Renderer&, const std::filesystem::path&, const SceneLayout&, std::string& error);
    void Draw(ID3D12GraphicsCommandList*,const SceneLayout&,unsigned int width,unsigned int height,const UiState& state={}) const;
    static UiState Defaults(const SceneLayout&);
    static UiRect Resolve(const SceneLayout&,const ScenePlacement&,unsigned int width,unsigned int height,const UiState& state={});
    static std::string Hit(const SceneLayout&,unsigned int width,unsigned int height,float x,float y,const UiState& state={},bool buttonsOnly=true);
    static UiEvent Activate(const SceneLayout&,const std::string& object,UiState& state);
    static std::string Shortcut(const SceneLayout&,const std::string& key,unsigned int width,unsigned int height,const UiState& state);
private:
    struct Resource {std::string signature; std::shared_ptr<Engine::SpriteRenderer> sprite;};
    void Prune(const Engine::DirectX12Renderer&,const SceneLayout&);
    void PreparePart(const Engine::DirectX12Renderer&,const std::filesystem::path&,const ScenePlacement&,bool text,std::map<std::string,Resource>& pending);
    void DrawPart(ID3D12GraphicsCommandList*,const ScenePlacement&,const UiRect&,unsigned int width,unsigned int height,const UiState&,bool text) const;
    std::map<std::string,Resource> resources_;
};
}
