#pragma once

#include <Engine/Graphics/Camera.h>
#include <array>

namespace Engine
{
    class DebugCamera final
    {
    public:
        /// <summary>
        /// 原点を正面に見る位置 (0, 0, -3.5) にカメラを生成します。
        /// </summary>
        DebugCamera() = default;

        /// <summary>
        /// 位置と角度を初期状態へ戻します。
        /// </summary>
        void Reset() noexcept;

        /// <summary>
        /// マウスの移動量をピクセル単位で受け取り、視点を回転します。上下角は反転を防ぐ範囲に制限します。
        /// </summary>
        void Rotate(float deltaX, float deltaY) noexcept;

        /// <summary>
        /// 左右・ワールド上下・前後の入力を -1～1 で指定して移動します。斜め移動の速度を正規化します。
        /// 経過秒数は有限の非負値を指定します。fast が true の場合は速度を 3 倍にします。
        /// </summary>
        void Move(float right, float up, float forward, double deltaSeconds, bool fast) noexcept;

        /// <summary>
        /// ワールド座標のカメラ位置を取得します。ライティングの視点位置にも使用できます。
        /// </summary>
        const std::array<float, 3>& GetPosition() const noexcept;

        /// <summary>
        /// 左手座標系のビュー行列を取得します。
        /// </summary>
        DirectX::XMMATRIX GetViewMatrix() const noexcept;

        /// <summary>
        /// 移動速度を毎秒 0.1～50 の範囲で設定します。範囲外の値は無視します。
        /// </summary>
        void SetMoveSpeed(float speed) noexcept;

        /// <summary>
        /// 毎秒の移動速度を取得します。
        /// </summary>
        float GetMoveSpeed() const noexcept;

        /// <summary>
        /// 共通カメラを取得します。座標・射影設定と描画に使用します。
        /// </summary>
        Camera& GetCamera() noexcept;
        /// <summary>
        /// 共通カメラを読み取り専用で取得します。
        /// </summary>
        const Camera& GetCamera() const noexcept;

    private:
        /// <summary>
        /// ヨー角とピッチ角から単位前方ベクトルを計算します。
        /// </summary>
        DirectX::XMVECTOR GetForward() const noexcept;

        Camera camera_;
        float yaw_ = 0.0f;
        float pitch_ = 0.0f;
        float moveSpeed_ = 3.0f;
    };
}
