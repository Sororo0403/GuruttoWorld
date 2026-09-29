#pragma once
#include <string>

namespace Engine
{
    class Keyboard;
    class DirectX12Renderer;
    enum class RenderResult;

    class IScene
    {
    public:
        /// <summary>
        /// シーンを破棄します。利用中の GPU 処理は事前に完了させてください。
        /// </summary>
        virtual ~IScene() = default;
        /// <summary>
        /// シーンのリソースを生成します。失敗時は false を返します。
        /// </summary>
        virtual bool Initialize(DirectX12Renderer& renderer) = 0;
        /// <summary>
        /// 状態を更新し、遷移先の識別子を返します。空文字列は現在のシーンを維持します。
        /// </summary>
        virtual std::string Update(double deltaSeconds, const Keyboard& keyboard) = 0;
        /// <summary>
        /// シーンを描画します。
        /// </summary>
        virtual RenderResult Draw(DirectX12Renderer& renderer) = 0;
    };
}
