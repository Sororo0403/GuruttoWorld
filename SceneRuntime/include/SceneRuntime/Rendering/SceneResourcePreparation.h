#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <Engine/Graphics/Renderers/ShadowMap.h>

namespace SceneRuntime
{
    struct SceneRenderData
    {
        std::vector<std::string> diagnostics;
        std::shared_ptr<Engine::AssetDatabase> assets;
        std::map<std::filesystem::path, std::shared_ptr<const Engine::Material>> materials;
        std::filesystem::path assetsRoot;
        std::shared_ptr<Engine::ShadowMap> shadow, localShadow;
        std::vector<Engine::Object3D> objects;
        std::map<std::string, std::shared_ptr<Engine::ModelRenderer>> animated;
        std::map<std::string, AnimatorState> states;
        std::map<std::string, std::vector<std::shared_ptr<const Engine::ModelRenderer>>> lods;
    };
    struct PreparedSceneResources : SceneRenderData
    {
        SceneLayout layout;
    };
    struct SceneResourceView
    {
        const decltype(PreparedSceneResources::layout)& layout;
        const decltype(PreparedSceneResources::objects)& objects;
        const decltype(PreparedSceneResources::assetsRoot)& root;
        const decltype(PreparedSceneResources::assets)& assets;
        const decltype(PreparedSceneResources::materials)& materials;
        const decltype(PreparedSceneResources::animated)& animated;
        const decltype(PreparedSceneResources::states)& states;
        const decltype(PreparedSceneResources::lods)& lods;
        std::shared_ptr<Engine::ShadowMap> shadow, localShadow;
    };
    struct SceneAnimatorFrame
    {
        std::string id;
        size_t index = 0;
        std::shared_ptr<Engine::ModelRenderer> model;
        AnimatorState state;
        Engine::ModelRenderer::PreparedPose pose;
        bool applyPose = false;
    };
}
