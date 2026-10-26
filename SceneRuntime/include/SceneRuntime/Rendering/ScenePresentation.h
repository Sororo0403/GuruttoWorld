#pragma once
#include <SceneRuntime/Rendering/RenderPresentation.h>
#include <SceneRuntime/Ui/SceneUi.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <Engine/Graphics/Renderers/ParticleRenderer.h>
#include <Engine/Graphics/Renderers/PostEffectRenderer.h>

namespace SceneRuntime
{
    class ScenePresentation final
    {
    public:
        /// <summary>固定した参照情報とデバイスを保持します。機能別の資源はPrepareで準備します。</summary>
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                        std::string& error, std::shared_ptr<const Engine::ContentContext> context = {});
        /// <summary>所有中の表示世代を描画adapterへ渡します。失敗時は未送信フレームを中止します。</summary>
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
        /// <summary>表示世代と準備済みの描画資源を、確保せずにまとめて取得します。</summary>
        PresentationPacket Capture(std::shared_ptr<const RenderPacket> packet) const noexcept;

    private:
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
        /// <summary>既定・独自の準備済み資源が変わった場合だけ、表示世代の共有参照を更新します。</summary>
        void RefreshResources() const;
        mutable std::shared_ptr<const PresentationResources> resources_;
        std::shared_ptr<const Engine::ContentContext> context_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        mutable std::shared_ptr<const Engine::Texture2D> environmentTexture_;
        std::filesystem::path root_, oceanShader_;
        mutable std::map<std::filesystem::path, std::shared_ptr<Engine::SpriteRenderer>> customSky_, customOcean_;
        mutable std::map<std::filesystem::path, std::shared_ptr<Engine::ParticleRenderer>> customParticles_;
        mutable std::map<std::filesystem::path, std::shared_ptr<Engine::PostEffectRenderer>> customPost_;
        mutable std::shared_ptr<Engine::SpriteRenderer> ocean_;
        mutable bool oceanReady_ = false;
        std::filesystem::path postShader_;
        mutable std::shared_ptr<Engine::PostEffectRenderer> postEffects_;
        SceneUi ui_;
        mutable bool skyReady_ = false, particlesReady_ = false;
        mutable std::shared_ptr<Engine::SpriteRenderer> sky_;
        mutable std::shared_ptr<Engine::ParticleRenderer> particles_;
    };
}
