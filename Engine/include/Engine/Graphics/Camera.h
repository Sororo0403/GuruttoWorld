#pragma once
#include <DirectXMath.h>
#include <array>

namespace Engine
{
    class Camera final
    {
    public:
        /// <summary>
        /// 原点の方向を見る既定のカメラを生成します。
        /// </summary>
        Camera();
        /// <summary>
        /// 有限のワールド座標を設定します。不正値は変更せず false を返します。
        /// </summary>
        bool SetPosition(const std::array<float, 3>& position);
        /// <summary>
        /// ヨー角・ピッチ角をラジアンで指定します。不正値は変更しません。
        /// </summary>
        bool SetRotation(float yaw, float pitch);
        /// <summary>
        /// 画角・縦横比・近遠クリップを設定します。不正値は変更しません。
        /// 縦横比と近遠クリップの差は DirectXMath の許容誤差 0.00001 より大きくしてください。
        /// </summary>
        bool SetPerspective(float verticalFov, float aspectRatio, float nearClip, float farClip);
        /// <summary>
        /// 画面サイズ変更時の縦横比を更新します。
        /// </summary>
        bool SetAspectRatio(float aspectRatio);
        /// <summary>
        /// ワールド座標を取得します。
        /// </summary>
        const std::array<float, 3>& GetPosition() const;
        /// <summary>
        /// ビュー行列を取得します。
        /// </summary>
        DirectX::XMMATRIX GetViewMatrix() const;
        /// <summary>
        /// 射影行列を取得します。
        /// </summary>
        DirectX::XMMATRIX GetProjectionMatrix() const;
        /// <summary>
        /// ビュー射影行列を取得します。
        /// </summary>
        DirectX::XMFLOAT4X4 GetViewProjectionMatrix() const;
    private:
        std::array<float, 3> position_{ 0.0f, 0.0f, -3.5f };
        float yaw_ = 0.0f;
        float pitch_ = 0.0f;
        float fov_ = DirectX::XM_PIDIV4;
        float aspect_ = 16.0f / 9.0f;
        float near_ = 0.1f;
        float far_ = 100.0f;
        DirectX::XMFLOAT4X4 projection_{};
    };
}
