#pragma once
#include <SceneRuntime/Rendering/SceneGpuResources.h>
#include <SceneRuntime/Rendering/EditorProjection.h>
#include <SceneRuntime/Rendering/RenderTelemetry.h>
#include <SceneRuntime/Scene/SceneChanges.h>
#include <span>

namespace SceneRuntime
{
    struct PresentationPacket;
    struct SceneDrawOptions;
    struct UiState;

    struct SceneRenderTransform
    {
        size_t index = 0;
        LocalTransform local;
        Engine::Object3D object;
    };

    /// <summary>シーン単位の表示資源・姿勢・世代を所有し、準備成功後の交換をまとめます。</summary>
    class SceneRenderState final
    {
    public:
        /// <summary>表示資源の準備先を取得します。確定済み配列の可変参照は公開しません。</summary>
        SceneGpuResources& Gpu() noexcept;
        /// <summary>確定済みの表示資源を読み取ります。</summary>
        const SceneRenderData& Read() const noexcept;
        /// <summary>保存データを借用し、現在の表示資源と対応する準備用ビューを作ります。</summary>
        SceneResourceView View(const SceneLayout& layout) const noexcept;
        /// <summary>明示的な構造変更・再読込のため、現在の資源を候補へ共有します。</summary>
        PreparedSceneResources Capture(SceneLayout layout) const;
        /// <summary>モデルと既定の影資源を初期化します。</summary>
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& shader,
                        std::string* error);
        /// <summary>現在の影を基準に、候補の設定に対応する影資源を準備します。</summary>
        bool PrepareShadows(PreparedSceneResources& prepared, std::string& error) const;
        /// <summary>旧資源の退役を確定前に準備します。失敗時は現在の表示を保持します。</summary>
        bool PrepareRetirement(PreparedSceneResources& prepared, std::string& error) const;
        /// <summary>交換前の描画個体を完了フェンスまで保持します。</summary>
        bool RetireObjects(const std::vector<Engine::Object3D>& objects) const;
        /// <summary>編集候補から表示packetを準備し、現在の世代は保持します。</summary>
        std::shared_ptr<const RenderPacket> PrepareEditor(const PreparedSceneResources& prepared,
                                                          std::string& error) const;
        /// <summary>退役の準備後に資源とpacketを交換し、旧packetを呼出側へ戻します。</summary>
        std::shared_ptr<const RenderPacket> CommitPrepared(PreparedSceneResources& prepared,
                                                           std::shared_ptr<const RenderPacket> packet,
                                                           bool preservePreview) noexcept;
        /// <summary>編集で変化した姿勢と表示packetを一緒に交換します。</summary>
        std::shared_ptr<const RenderPacket> CommitTransforms(std::vector<Engine::Object3D>& objects,
                                                             std::shared_ptr<const RenderPacket> packet) noexcept;
        /// <summary>実行フレームの資源・差分姿勢・骨格・packetを確保なしで確定します。</summary>
        std::shared_ptr<const RenderPacket> CommitFrame(PreparedSceneResources* prepared,
                                                        std::span<SceneRenderTransform> transforms,
                                                        std::span<SceneAnimatorFrame> poses,
                                                        std::shared_ptr<const RenderPacket> packet,
                                                        SceneRenderState* reload = nullptr) noexcept;
        /// <summary>再取得した資源世代を交換し、描画中の旧packetの所有権は保持します。</summary>
        std::shared_ptr<const RenderPacket> CommitReload(SceneRenderState& candidate,
                                                         std::shared_ptr<const RenderPacket> packet) noexcept;
        /// <summary>現在の不変な表示packetを取得します。</summary>
        const std::shared_ptr<const RenderPacket>& Snapshot() const noexcept;
        /// <summary>表示が更新された世代番号を取得します。</summary>
        size_t Version() const noexcept;
        /// <summary>編集プレビュー用の領域へ現在の保存値を同期します。</summary>
        SceneLayout& AnimationScratch(const SceneLayout& layout);
        /// <summary>編集プレビューの一時領域を解放します。</summary>
        void ResetPreview() noexcept;
        /// <summary>確定済みの数値変更に対応する材質を表示個体へ反映します。</summary>
        void ApplySurface(size_t index, const ScenePlacement& placement);
        /// <summary>検証・通知準備済みのプレビューパラメーターを確保なしで交換します。</summary>
        void CommitAnimatorParameters(const std::string& id, std::map<std::string, float>& values) noexcept;
        /// <summary>プレビュー用のパラメーター上書きを削除します。</summary>
        void EraseAnimatorParameter(const std::string& id, const std::string& name) noexcept;
        /// <summary>文書への追加を確定する前に描画個体の格納領域を予約します。</summary>
        void ReserveAdditionalObject();
        /// <summary>予約済みの領域へ追加個体を反映し、表示世代を進めます。</summary>
        void AppendObject(Engine::Object3D object);
        /// <summary>追加個体の骨格・LOD資源の所有権を引き継ぎます。</summary>
        void MergeAnimation(PreparedSceneResources& prepared);
        /// <summary>準備した複製個体と姿勢・LODを確保なしで交換します。</summary>
        void CommitDuplicates(
            std::vector<Engine::Object3D>& objects,
            std::map<std::string, std::shared_ptr<Engine::ModelRenderer>>& animated,
            std::map<std::string, AnimatorState>& states,
            std::map<std::string, std::vector<std::shared_ptr<const Engine::ModelRenderer>>>& lods) noexcept;
        /// <summary>補間行列が現在の表示世代と一致するか返します。</summary>
        bool MatricesCurrent() const noexcept;
        /// <summary>保存値と補間姿勢から、現在の表示世代の行列を準備します。</summary>
        bool PrepareMatrices(const SceneLayout& layout, const std::map<std::string, DirectX::XMFLOAT4X4>& overrides,
                             std::string& error) const;
        /// <summary>準備済みの描画行列を取得します。</summary>
        const std::vector<DirectX::XMFLOAT4X4>& Matrices() const noexcept;
        /// <summary>最後に記録した描画統計を取得します。</summary>
        const MeshTelemetry& Telemetry() const noexcept;
        /// <summary>現在のpacketを描画し、可視判定と統計を更新します。</summary>
        bool DrawScene(ID3D12GraphicsCommandList* commands, const Engine::Camera& camera,
                       const Engine::DirectionalLight& light, const UiState& state) const;
        /// <summary>準備済みの演出packetを描画し、可視判定と統計を更新します。</summary>
        bool DrawPresentation(const PresentationPacket& packet, ID3D12GraphicsCommandList* commands, unsigned int width,
                              unsigned int height, const SceneDrawOptions& options) const;

    private:
        /// <summary>資源配列と参照情報を確保なしで交換します。</summary>
        void SwapData(SceneRenderData& data) noexcept;
        /// <summary>新しいpacketと世代を公開し、旧packetの破棄を呼出側へ委ねます。</summary>
        std::shared_ptr<const RenderPacket> Publish(std::shared_ptr<const RenderPacket> packet) noexcept;
        SceneGpuResources gpu_;
        SceneRenderData data_;
        EditorProjection editor_;
        std::shared_ptr<const RenderPacket> packet_;
        std::unique_ptr<SceneLayout> animationScratch_;
        size_t version_ = 0;
        mutable size_t matricesVersion_ = static_cast<size_t>(-1);
        mutable std::vector<DirectX::XMFLOAT4X4> matrices_;
        mutable MeshTelemetry telemetry_;
    };
}
