#pragma once
#include <Engine/Assets/ContentContext.h>
#include <SceneRuntime/Scene/SceneLayout.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <map>
#include <Engine/Graphics/DirectX12/GpuRetirement.h>
namespace Engine
{
    class DirectX12Renderer;
    class Camera;
}
namespace SceneRuntime
{
    class SceneWorld;
    struct UiState
    {
        std::map<std::string, float> values;
        std::map<std::string, bool> visibility;
        std::string hovered, pressed;
        std::map<std::string, std::string> strings;
        std::map<std::string, std::array<float, 2>> scrollOffsets;
        std::string focused;
        std::array<float, 2> dragPosition{}, dragScroll{};
        float Value(const std::string& key, float fallback = 0) const;
        bool Matches(const std::string& expression) const;
        bool Assign(const std::string& expression);
    };
    struct UiRect
    {
        std::array<float, 2> position{}, size{};
        float rotation = 0, opacity = 1, scale = 1;
        bool visible = true;
        std::array<float, 4> clip{-100000, -100000, 100000, 100000};
        bool Contains(float x, float y) const;
    };
    struct UiEvent
    {
        std::string object, action, target, sound, event;
        float value = 0;
        std::string text;
    };
    struct UiPointerInput
    {
        float x = 0, y = 0;
        bool down = false, pressed = false, released = false;
        float wheel = 0;
    };
    class SceneUi final
    {
    public:
        /// <summary>変更した UI 資源を準備します。実行中はシェーダー検査を省き、明示再読込時に更新を反映できます。</summary>
        /// <param name="inspectShaders">true の場合は依存シェーダーの変更を検査します。初回やデバイス・ルート変更時は必ず検査します。</param>
        bool Prepare(const Engine::DirectX12Renderer&, const std::filesystem::path&, const SceneLayout&,
                     std::string& error, const UiState& state = {}, bool inspectShaders = true,
                     std::shared_ptr<const Engine::ContentContext> context = {});
        void Draw(ID3D12GraphicsCommandList*, const SceneLayout&, unsigned int width, unsigned int height,
                  const UiState& state = {}) const;
        void DrawScene(ID3D12GraphicsCommandList*, const SceneWorld&, const Engine::Camera&,
                       const UiState& state = {}) const;
        static UiState Defaults(const SceneLayout&);
        /// <summary>シーン切り替えの初期状態へ、保持したUIの実行値を引き継ぎます。</summary>
        static UiState SceneState(const SceneLayout& previousLayout, const SceneLayout& layout, const UiState& previous,
                                  bool reset);
        static UiRect Resolve(const SceneLayout&, const ScenePlacement&, unsigned int width, unsigned int height,
                              const UiState& state = {});
        static std::string Hit(const SceneLayout&, unsigned int width, unsigned int height, float x, float y,
                               const UiState& state = {}, bool buttonsOnly = true);
        static UiEvent Activate(const SceneLayout&, const std::string& object, UiState& state);
        /// <summary>クリック開始・ドラッグ・解放・ホイールを共通UIの状態へ適用します。</summary>
        static UiEvent Pointer(const SceneLayout&, unsigned int width, unsigned int height, const UiPointerInput& input,
                               UiState& state);
        /// <summary>UTF-32文字入力をフォーカス中の入力欄へ反映します。</summary>
        static UiEvent TextInput(const SceneLayout&, char32_t character, UiState& state);
        /// <summary>実行状態を反映した入力欄の表示文字列を取得します。</summary>
        static std::string InputText(const ScenePlacement&, const UiState&);
        static std::string Shortcut(const SceneLayout&, const std::string& key, unsigned int width, unsigned int height,
                                    const UiState& state);

    private:
        std::shared_ptr<const Engine::ContentContext> content_;
        struct Resource
        {
            std::size_t generation = 0;
            std::string signature;
            std::shared_ptr<Engine::SpriteRenderer> sprite, sceneSprite;
            std::size_t textBytes = 0;
        };
        bool ReusePart(const std::string& key, const std::string& signature, const ScenePlacement& p, bool text,
                       std::size_t generation, std::map<std::string, Resource>& pending) const;
        std::pair<std::shared_ptr<Engine::SpriteRenderer>, std::shared_ptr<Engine::SpriteRenderer>> FindPrototypes(
            std::size_t generation, const std::map<std::string, Resource>& pending) const;
        void DrawCanvasParts(ID3D12GraphicsCommandList* commands, const ScenePlacement& p, const UiRect& rect,
                             const DirectX::XMFLOAT4X4& projection, const UiState& state) const;
        struct CachedText
        {
            Resource resource;
            std::size_t lastUse = 0;
        };
        struct Preparation
        {
            bool inspected = false;
            std::string uiRevision, sceneRevision;
            std::filesystem::path preparedRoot;
            std::size_t generation = 0;
            std::map<std::string, Resource> pending;
            bool cacheChanged = false;
            std::map<std::string, CachedText> cache;
            std::size_t cacheClock = 0;
            std::shared_ptr<Engine::GpuRetirement> retirement;
        };
        void InspectContext(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                            bool inspectShaders, Preparation& candidate) const;
        void PrepareResources(const Engine::DirectX12Renderer& renderer, const SceneLayout& layout,
                              const UiState& state, Preparation& candidate);
        void PrepareTextCache(Preparation& candidate) const;
        void RetireReplaced(Preparation& candidate) const;
        void CommitPreparation(const Engine::DirectX12Renderer& renderer, Preparation& candidate);
        /// <summary>表示内容が変化した UI 資源だけを候補へ準備します。</summary>
        void PreparePart(const Engine::DirectX12Renderer&, const std::filesystem::path&, const ScenePlacement&,
                         bool text, std::size_t generation, std::map<std::string, Resource>& pending);
        void DrawPart(ID3D12GraphicsCommandList*, const ScenePlacement&, const UiRect&, unsigned int width,
                      unsigned int height, const UiState&, bool text) const;
        std::filesystem::path preparedRoot_;
        ID3D12Device* preparedDevice_ = nullptr;
        ID3D12CommandQueue* preparedQueue_ = nullptr;
        std::string uiRevision_, sceneRevision_;
        std::size_t contextGeneration_ = 0;
        std::shared_ptr<Engine::GpuRetirement> retirement_;
        std::map<std::string, Resource> resources_;
        // 表示中の資源とは別に、再表示する文字だけを上限付きで保持します。
        std::map<std::string, CachedText> textCache_;
        std::size_t textCacheClock_ = 0;
    };
}
