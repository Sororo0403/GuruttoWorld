#pragma once
#include <SceneRuntime/SceneView.h>
#include <SceneRuntime/SceneUi.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <Engine/Graphics/Renderers/ParticleRenderer.h>
#include <Engine/Graphics/Renderers/PostEffectRenderer.h>

namespace SceneRuntime
{
    class ScenePresentation final
    {
    public:
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root, std::string& error);
        void Draw(ID3D12GraphicsCommandList* commands, const SceneWorld& world, unsigned int width,
            unsigned int height, const Engine::Camera* sceneCamera=nullptr, double seconds=0, bool motionEnabled=true, const UiState* uiState=nullptr, bool showSceneUi=true) const;
        /// <summary>UI を準備します。明示的な初期化・再読込ではシェーダーの変更も検査します。</summary>
        bool PrepareUi(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,const SceneLayout& layout,std::string& error,const UiState& state={},bool inspectShaders=true)
        { return PrepareEffects(layout,error) && ui_.Prepare(renderer,root,layout,error,state,inspectShaders); }
        /// <summary>現在のUI描画資源を変更せず、候補へ描画資源を準備します。</summary>
        bool PrepareUiCandidate(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,const SceneLayout& layout,std::string& error,const UiState& state,SceneUi& candidate)
        {return PrepareEffects(layout,error) && candidate.Prepare(renderer,root,layout,error,state,false);}
        /// <summary>シーン変更に失敗した場合に戻せるUI描画資源の共有参照を取得します。</summary>
        SceneUi CaptureUi() const { return SceneUi{ui_}; }
        /// <summary>保存したUI描画資源をファイルの再読み込みなしで復元します。GPU同期後に呼び出します。</summary>
        void RestoreUi(SceneUi previous) { ui_=std::move(previous); }
        void DrawUi(ID3D12GraphicsCommandList* commands,const SceneLayout& layout,unsigned int width,unsigned int height,const UiState& state={}) const
        { ui_.Draw(commands,layout,width,height,state); }
        static std::array<float,4> Particle(const SceneWorld& world, const ScenePlacement& placement,
            unsigned int index, double seconds);
    private:
        /// <summary>有効化したシーンにだけポストエフェクトのパイプラインを準備します。</summary>
        bool PrepareEffects(const SceneLayout& layout,std::string& error) const;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        std::shared_ptr<const Engine::Texture2D> environmentTexture_;
        std::filesystem::path root_,oceanShader_;
        mutable std::map<std::filesystem::path,std::shared_ptr<Engine::SpriteRenderer>> customSky_,customOcean_;
        mutable std::map<std::filesystem::path,std::shared_ptr<Engine::ParticleRenderer>> customParticles_;
        mutable std::map<std::filesystem::path,std::shared_ptr<Engine::PostEffectRenderer>> customPost_;
        mutable Engine::SpriteRenderer ocean_;
        mutable bool oceanReady_=false;
        std::filesystem::path postShader_;
        mutable Engine::PostEffectRenderer postEffects_;
        void DrawSky(ID3D12GraphicsCommandList* commands, const SceneLayout& layout,
            unsigned int width, unsigned int height, double seconds,const Engine::Camera* camera=nullptr) const;
        void DrawParticles(ID3D12GraphicsCommandList* commands, const SceneWorld& world,
            const Engine::Camera& camera, double seconds) const;
        SceneUi ui_;
        Engine::SpriteRenderer sky_;
        Engine::ParticleRenderer particles_;
    };
}
