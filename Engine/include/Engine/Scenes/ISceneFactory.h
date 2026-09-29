#pragma once
#include <Engine/Scenes/IScene.h>
#include <memory>
#include <string_view>

namespace Engine
{
    class ISceneFactory
    {
    public:
        /// <summary>
        /// Factory を破棄します。
        /// </summary>
        virtual ~ISceneFactory() = default;
        /// <summary>
        /// 識別子に対応する未初期化のシーンを生成します。不明な識別子は空を返します。
        /// </summary>
        virtual std::unique_ptr<IScene> Create(std::string_view name) = 0;
    };
}
