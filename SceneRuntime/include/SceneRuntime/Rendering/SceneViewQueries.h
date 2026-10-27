#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <DirectXMath.h>
#include <map>
#include <optional>

namespace SceneRuntime
{
    class SceneRenderState;

    struct SceneViewPose
    {
        std::array<float, 3> position{}, rotation{}, scale{1, 1, 1};
        std::string parent;
    };

    struct SceneViewInterpolation
    {
        const std::map<std::string, SceneViewPose>* previous = nullptr;
        const std::map<std::string, SceneViewPose>* current = nullptr;
        float alpha = 0;
    };

    /// <summary>確定済み配置・表示資源・補間姿勢を借用する読取専用の問い合わせです。次の更新前まで利用できます。</summary>
    class SceneViewQueries final
    {
    public:
        /// <summary>Worldを所有せず、ホストが保持する表示情報を借用します。</summary>
        SceneViewQueries(const SceneLayout& layout, const SceneRenderState& renderState,
                         SceneViewInterpolation interpolation = {}) noexcept;
        /// <summary>選択ガイドやCanvasの問い合わせに使う確定済み配置を読み取ります。</summary>
        const SceneLayout& Layout() const noexcept;
        /// <summary>有効な描画形状との最も近い交点を探します。距離はワールド単位です。</summary>
        std::optional<std::string> PickRay(const std::array<float, 3>& origin, const std::array<float, 3>& direction,
                                           float maxDistance = 220.0f) const;
        /// <summary>祖先の回転を合成し、スケール・反転・せん断を除いた回転を取得します。</summary>
        bool WorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        /// <summary>物理補間した祖先の回転を合成します。</summary>
        bool RenderWorldRotation(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        /// <summary>確定済みの表示個体からワールド行列を取得します。補間は適用しません。</summary>
        bool WorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        /// <summary>インポート設定を含む骨格座標から世界座標への行列を取得します。</summary>
        bool SkeletonWorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        /// <summary>確定済み配置を変えず、物理補間した表示行列を取得します。</summary>
        bool RenderWorldMatrix(std::string_view id, DirectX::XMFLOAT4X4& matrix) const;
        /// <summary>ワールド行列を親基準のローカル姿勢へ変換します。表現できないせん断は拒否します。</summary>
        bool LocalTransformFromWorld(std::string_view id, const DirectX::XMFLOAT4X4& world,
                                     ScenePlacement& placement) const;
        /// <summary>確定済み姿勢でモデル境界箱の八頂点を変換します。モデルなしでは選択用の小さな箱を使います。</summary>
        bool WorldBounds(std::string_view id, std::array<std::array<float, 3>, 8>& corners) const;

    private:
        /// <summary>借用した物理姿勢から補間可能なローカル行列と回転を求めます。</summary>
        bool InterpolatedLocalTransform(const ScenePlacement& placement, DirectX::XMFLOAT4X4& transform,
                                        DirectX::XMFLOAT4X4& rotation) const;
        /// <summary>表示世代に対応する補間行列を読取キャッシュへ準備します。</summary>
        bool PrepareRenderTransforms() const;
        const SceneLayout& layout_;
        const SceneRenderState& renderState_;
        SceneViewInterpolation interpolation_;
    };
}
