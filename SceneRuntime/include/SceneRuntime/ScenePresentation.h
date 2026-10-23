#pragma once
#include <SceneRuntime/SceneView.h>
#include <SceneRuntime/SceneUi.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <Engine/Graphics/Renderers/ParticleRenderer.h>
#include <Engine/Graphics/Renderers/PostEffectRenderer.h>

namespace SceneRuntime
{
    struct SceneDrawOptions
    {
        const Engine::Camera* sceneCamera = nullptr;
        double seconds = 0;
        bool motionEnabled = true;
        const UiState* uiState = nullptr;
        bool showSceneUi = true;
        bool showGameUi = true;
    };
    class ScenePresentation final
    {
    public:
        /// <summary>固定した参照情報とデバイスを保持します。機能別の資源はPrepareで準備します。</summary>
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                        std::string& error, std::shared_ptr<const Engine::ContentContext> context = {});
        void Draw(ID3D12GraphicsCommandList* commands, const SceneWorld& world, unsigned int width, unsigned int height,
                  const SceneDrawOptions& options = {}) const;
        /// <summary>使用する機能の描画資源とUIを描画前に準備します。明示的な再読込ではシェーダーも検査します。</summary>
        bool Prepare(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                     const SceneLayout& layout, std::string& error, const UiState& state = {},
                     bool inspectShaders = true)
        {
            return PrepareEffects(layout, error) &&
                   ui_.Prepare(renderer, root, layout, error, state, inspectShaders, context_);
        }
        /// <summary>現在のUI描画資源を変更せず、候補へ描画資源を準備します。</summary>
        bool PrepareCandidate(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                              const SceneLayout& layout, std::string& error, const UiState& state, SceneUi& candidate)
        {
            return PrepareEffects(layout, error) &&
                   candidate.Prepare(renderer, root, layout, error, state, false, context_);
        }
        /// <summary>シーン変更に失敗した場合に戻せるUI描画資源の共有参照を取得します。</summary>
        SceneUi CaptureUi() const
        {
            return SceneUi{ui_};
        }
        /// <summary>保存したUI描画資源をファイルの再読み込みなしで復元します。GPU同期後に呼び出します。</summary>
        void RestoreUi(SceneUi previous)
        {
            ui_ = std::move(previous);
        }
        void DrawUi(ID3D12GraphicsCommandList* commands, const SceneLayout& layout, unsigned int width,
                    unsigned int height, const UiState& state = {}) const
        {
            ui_.Draw(commands, layout, width, height, state);
        }
        static std::array<float, 4> Particle(const SceneWorld& world, const ScenePlacement& placement,
                                             unsigned int index, double seconds);

    private:
        bool BeginPostEffects(ID3D12GraphicsCommandList* commands, const SceneLayout& layout, unsigned int width,
                              unsigned int height, Engine::PostEffectRenderer*& post) const;
        /// <summary>有効化したシーンにだけポストエフェクトのパイプラインを準備します。</summary>
        bool PrepareEffects(const SceneLayout& layout, std::string& error) const;
        /// <summary>標準の空と粒子の資源を必要時だけ準備します。</summary>
        bool PrepareDefaultSprites(const SceneLayout& layout, bool sky, bool particles, std::string& error) const;
        /// <summary>独自の空と海のシェーダーを準備します。</summary>
        bool PrepareCustomSprites(const SceneLayout& layout, bool sky, bool ocean, std::string& error) const;
        /// <summary>独自の粒子とポストエフェクトを準備します。</summary>
        bool PrepareCustomEffects(const SceneLayout& layout, bool particles, std::string& error) const;
        /// <summary>標準の海とポストエフェクトを準備します。</summary>
        bool PrepareDefaultEffects(const SceneLayout& layout, bool ocean, std::string& error) const;
        /// <summary>シーンとゲームのUI表示条件に従って重ね描きします。</summary>
        void DrawOverlay(ID3D12GraphicsCommandList* commands, const SceneWorld& world, unsigned int width,
                         unsigned int height, const SceneDrawOptions& options) const;
        std::shared_ptr<const Engine::ContentContext> context_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        mutable std::shared_ptr<const Engine::Texture2D> environmentTexture_;
        std::filesystem::path root_, oceanShader_;
        mutable std::map<std::filesystem::path, std::shared_ptr<Engine::SpriteRenderer>> customSky_, customOcean_;
        mutable std::map<std::filesystem::path, std::shared_ptr<Engine::ParticleRenderer>> customParticles_;
        mutable std::map<std::filesystem::path, std::shared_ptr<Engine::PostEffectRenderer>> customPost_;
        mutable Engine::SpriteRenderer ocean_;
        mutable bool oceanReady_ = false;
        std::filesystem::path postShader_;
        mutable Engine::PostEffectRenderer postEffects_;
        void DrawSky(ID3D12GraphicsCommandList* commands, const SceneLayout& layout, unsigned int width,
                     unsigned int height, double seconds, const Engine::Camera* camera = nullptr) const;
        void DrawParticles(ID3D12GraphicsCommandList* commands, const SceneWorld& world, const Engine::Camera& camera,
                           double seconds) const;
        SceneUi ui_;
        mutable bool skyReady_ = false, particlesReady_ = false;
        mutable Engine::SpriteRenderer sky_;
        mutable Engine::ParticleRenderer particles_;
    };
}
