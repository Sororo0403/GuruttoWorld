#pragma once

#include <Engine/Graphics/Materials/DirectionalLight.h>
#include <Engine/Graphics/Materials/UvTransform.h>
#include <Engine/Graphics/Materials/Material.h>
#include <DirectXMath.h>
#include <array>
#include <memory>

struct ID3D12GraphicsCommandList;

namespace Engine
{
    class ModelRenderer;
    class Camera;

    class Object3D final
    {
    public:
        /// <summary>
        /// 単位ワールド行列を持つオブジェクトを生成します。
        /// </summary>
        Object3D();

        /// <summary>
        /// 共有モデルを切り替えます。変換は維持し、空を指定すると描画を停止します。
        /// 最後の参照を解放する前に GPU 処理を完了させるか、ModelManager に所有権を保持させてください。
        /// </summary>
        void SetModel(std::shared_ptr<const ModelRenderer> model);
        void SetMaterial(std::shared_ptr<const Material> material) { material_=std::move(material); }
        const std::shared_ptr<const Material>& GetMaterial() const { return material_; }
        void SetMaterialSlots(MaterialSlots slots) {materialSlots_=std::move(slots);}
        const MaterialSlots& GetMaterialSlots() const {return materialSlots_;}
        const Material* MaterialForMesh(size_t index) const {return MaterialForSlot(index,material_.get(),materialSlots_);}
        bool HasMaterialPass(MaterialPass pass) const;

        /// <summary>
        /// 現在の共有モデルを取得します。
        /// </summary>
        const std::shared_ptr<const ModelRenderer>& GetModel() const;

        /// <summary>
        /// 座標・回転（ラジアン）・拡縮を設定し、ワールド行列を更新します。
        /// 非有限値・ゼロ拡縮・逆行列を計算できない変換は false を返し、直前の状態を維持します。
        /// </summary>
        bool SetTransform(const std::array<float, 3>& position, const std::array<float, 3>& rotation,
            const std::array<float, 3>& scale);

        // Set the full affine draw matrix, retaining shear. SRT getters retain the values from SetTransform.
        // Invalid matrices leave the previous draw matrix unchanged.
        bool SetWorldMatrix(const DirectX::XMFLOAT4X4& matrix);

        /// <summary>
        /// オブジェクトの座標を取得します。
        /// </summary>
        const std::array<float, 3>& GetPosition() const;

        /// <summary>
        /// X・Y・Z 軸周りの回転角（ラジアン）を取得します。
        /// </summary>
        const std::array<float, 3>& GetRotation() const;

        /// <summary>
        /// 各軸の拡縮率を取得します。
        /// </summary>
        const std::array<float, 3>& GetScale() const;

        /// <summary>
        /// オブジェクトが保持するワールド行列を取得します。
        /// </summary>
        const DirectX::XMFLOAT4X4& GetWorldMatrix() const;

        /// <summary>
        /// 保持するワールド行列と共有モデルで描画します。モデル未設定時は何もしません。
        /// 描画リソースを解放する前に、利用中の GPU 処理を完了させてください。
        /// </summary>
        void Draw(ID3D12GraphicsCommandList* commands, const Camera& camera,
            const DirectionalLight& light = {},
            const UvTransform& uvTransform = {},MaterialPass pass=MaterialPass::All) const;

    private:
        std::shared_ptr<const ModelRenderer> model_;
        std::shared_ptr<const Material> material_;
        MaterialSlots materialSlots_;
        std::array<float, 3> position_{};
        std::array<float, 3> rotation_{};
        std::array<float, 3> scale_{ 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4X4 world_{};
    };
}
