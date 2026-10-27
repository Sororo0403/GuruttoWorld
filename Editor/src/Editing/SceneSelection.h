#pragma once
#include "EditState.h"
#include <SceneRuntime/Rendering/SceneViewQueries.h>
#include "../Viewport/SceneViewport.h"
#include <Engine/Graphics/Camera.h>

namespace Editor
{
    class SceneSelection final
    {
    public:
        /// <summary>表示形状のピッキング結果を選択へ反映します。</summary>
        static void Update(const SceneRuntime::SceneViewQueries& view, const Engine::Camera& camera, EditState& state,
                           const SceneViewport& viewport, bool active);
        /// <summary>表示問い合わせから選択境界と成分ガイドを描画します。</summary>
        static void Draw(const SceneRuntime::SceneViewQueries& view, const Engine::Camera& camera,
                         const EditState& state, const SceneViewport& viewport);

    private:
        /// <summary>一つの選択対象の境界と成分ガイドを描画します。</summary>
        static void DrawObject(const SceneRuntime::SceneViewQueries& view, const Engine::Camera& camera,
                               const std::string& id, const SceneViewport& viewport, bool primary);
    };
}
