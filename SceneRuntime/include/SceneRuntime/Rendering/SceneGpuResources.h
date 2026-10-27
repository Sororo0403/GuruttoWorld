#pragma once
#include <SceneRuntime/Rendering/SceneResourcePreparation.h>
#include <Engine/Graphics/DirectX12/GpuRetirement.h>
#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Graphics/Renderers/OcclusionRenderer.h>

namespace Engine
{
    class DirectX12Renderer;
}
namespace SceneRuntime
{
    /// <summary>GPU基盤とモデルキャッシュを所有し、モデル・材質・影の交換候補を準備します。</summary>
    class SceneGpuResources final
    {
        using PreparedLayout = PreparedSceneResources;
        using LayoutView = SceneResourceView;

    public:
        /// <summary>描画デバイスとモデル・影・可視判定の資源を初期化します。</summary>
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& shaderPath,
                        std::string* diagnostic);
        /// <summary>保存データと描画資源の候補を準備し、公開中の状態は変更しません。</summary>
        bool PrepareLayout(SceneLayout layout, const std::filesystem::path& assetsRoot, std::string& error,
                           bool preserveExecution, PreparedLayout& result, const PreparedLayout* previous,
                           const LayoutView& baseline, bool editing,
                           std::shared_ptr<const Engine::ContentContext>& context);
        /// <summary>設定に対応する影資源の交換候補を準備します。</summary>
        bool PrepareShadows(const SceneSettings& settings, std::shared_ptr<Engine::ShadowMap>& directional,
                            std::shared_ptr<Engine::ShadowMap>& local, std::string& error) const;
        /// <summary>追加個体のモデル・材質・骨格資源を準備します。</summary>
        Engine::Object3D BuildAddedObject(const ScenePlacement& placement, const SceneLayout& validation,
                                          const DirectX::XMFLOAT4X4& matrix, const std::filesystem::path& assetsRoot,
                                          PreparedLayout& prepared,
                                          const std::shared_ptr<const Engine::ContentContext>& context);
        /// <summary>再生中の静的個体のモデルと材質を準備します。</summary>
        Engine::Object3D BuildStaticRuntimeObject(const ScenePlacement& p, PreparedLayout& candidate,
                                                  const std::filesystem::path& assetsRoot,
                                                  const std::shared_ptr<const Engine::ContentContext>& context);
        /// <summary>描画デバイスの所有権を取得します。</summary>
        const Microsoft::WRL::ComPtr<ID3D12Device>& Device() const noexcept
        {
            return materialDevice_;
        }
        /// <summary>資源転送に用いるキューの所有権を取得します。</summary>
        const Microsoft::WRL::ComPtr<ID3D12CommandQueue>& Queue() const noexcept
        {
            return materialQueue_;
        }
        /// <summary>フェンスに従う資源の退役先を取得します。</summary>
        const std::shared_ptr<Engine::GpuRetirement>& Retirement() const noexcept
        {
            return retirement_;
        }
        /// <summary>描画資源の初期化が完了しているか返します。</summary>
        bool Ready() const noexcept
        {
            return modelsReady_;
        }
        /// <summary>ビューごとの可視判定を取得します。</summary>
        Engine::OcclusionRenderer& Occlusion() const noexcept
        {
            return occlusion_;
        }
        /// <summary>再読込の成功時だけモデル世代と初期化結果を交換します。</summary>
        void SwapModels(SceneGpuResources& candidate) noexcept;
        /// <summary>複製元の骨格姿勢から、複製先専用のGPU資源を準備します。</summary>
        void DuplicateAnimator(const Engine::Object3D& source, const AnimatorState& sourceState, const std::string& id,
                               Engine::Object3D& object,
                               std::map<std::string, std::shared_ptr<Engine::ModelRenderer>>& animated,
                               std::map<std::string, AnimatorState>& states);
        /// <summary>描画個体に保存された材質の数値上書きを適用します。</summary>
        static void ApplySurfaceOverride(Engine::Object3D& object, const ScenePlacement& placement);

    private:
        /// <summary>配置と候補の資源表から描画個体を準備します。</summary>
        Engine::Object3D BuildLayoutObject(const ScenePlacement& placement, const SceneLayout& layout,
                                           const DirectX::XMFLOAT4X4& matrix, PreparedLayout& prepared,
                                           const LayoutView& baseline, bool preserveExecution,
                                           const std::shared_ptr<const Engine::ContentContext>& context);
        /// <summary>既存の実行姿勢を保持しながら候補の骨格資源を準備します。</summary>
        void InitializeAnimatorObject(Engine::Object3D& object, const ScenePlacement& placement,
                                      const SceneLayout& layout, const DirectX::XMFLOAT4X4& matrix,
                                      PreparedLayout& prepared, const LayoutView& baseline, bool preserveExecution);
        /// <summary>候補の素材参照を解決し、編集時の欠損を診断へ残します。</summary>
        bool PrepareLayoutAssets(SceneLayout& layout, PreparedLayout& prepared, const PreparedLayout* previous,
                                 bool configurationCurrent, std::map<std::string, std::string>& unresolved,
                                 bool editing, const std::shared_ptr<const Engine::ContentContext>& context);
        /// <summary>編集表示で利用できない参照を診断します。</summary>
        void ValidateEditingResources(const ScenePlacement& placement, bool indexAvailable,
                                      const std::map<std::string, std::string>& unresolved, bool editing) const;
        /// <summary>描画個体を準備し、編集時の欠損は代替表示へ置き換えます。</summary>
        void PrepareLayoutObjects(const SceneLayout& layout, const std::vector<DirectX::XMFLOAT4X4>& matrices,
                                  PreparedLayout& prepared, const LayoutView& baseline, bool preserveExecution,
                                  bool indexAvailable, const std::map<std::string, std::string>& unresolved,
                                  bool editing, const std::shared_ptr<const Engine::ContentContext>& context);
        /// <summary>追加個体の初期骨格姿勢を準備します。</summary>
        void InitializeAddedAnimator(Engine::Object3D& object, const ScenePlacement& placement,
                                     const SceneLayout& validation, const DirectX::XMFLOAT4X4& matrix,
                                     const std::shared_ptr<const Engine::ModelRenderer>& model,
                                     PreparedLayout& prepared);
        std::shared_ptr<Engine::GpuRetirement> retirement_;
        Microsoft::WRL::ComPtr<ID3D12Device> materialDevice_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> materialQueue_;
        bool modelsReady_ = false;
        std::filesystem::path meshShader_;
        mutable Engine::OcclusionRenderer occlusion_;
        Engine::ModelManager models_;
    };
}
