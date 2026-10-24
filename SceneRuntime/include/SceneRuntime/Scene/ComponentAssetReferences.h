#pragma once
#include <SceneRuntime/Scene/SceneComponents.h>
#include <SceneRuntime/Gameplay/GenreComponents.h>
#include <SceneRuntime/Ui/UiComponents.h>

namespace SceneRuntime
{
    /// <summary>メッシュ本体とLODのモデル素材を走査します。</summary>
    template <class Visitor> void ComponentAssetReferences(MeshRendererComponent& component, Visitor&& visitor)
    {
        visitor(component.model);
        for (auto& lod : component.lods)
        {
            visitor(lod.model);
        }
    }
    /// <summary>マテリアル本体とサブメッシュの割り当てを走査します。</summary>
    template <class Visitor> void ComponentAssetReferences(MaterialComponent& component, Visitor&& visitor)
    {
        visitor(component.asset);
        for (auto& slot : component.slots)
        {
            visitor(slot);
        }
    }
    /// <summary>Collider生成に使うモデル素材を走査します。</summary>
    template <class Visitor> void ComponentAssetReferences(BoxColliderComponent& component, Visitor&& visitor)
    {
        visitor(component.model);
    }
    /// <summary>保存データで明示されたシーン素材だけを走査し、表示用文字列を保持します。</summary>
    template <class Visitor> void ComponentAssetReferences(ScriptComponent& component, Visitor&& visitor)
    {
        for (const auto& name : component.sceneAssets)
        {
            const auto found = component.data.find(name);
            if (found == component.data.end() || !std::holds_alternative<std::string>(found->second.value))
            {
                throw std::runtime_error("Invalid Script scene asset field");
            }
            visitor(std::get<std::string>(found->second.value));
        }
    }
    /// <summary>メニューの遷移先・設定画面・戻り先のシーン素材を走査します。</summary>
    template <class Visitor> void ComponentAssetReferences(CanvasComponent& component, Visitor&& visitor)
    {
        if (!component.menu)
        {
            return;
        }
        auto& menu = *component.menu;
        visitor(menu.returnScene);
        for (auto& entry : menu.entries)
        {
            if (entry.action == "loadScene" || entry.action == "settings")
            {
                visitor(entry.target);
            }
        }
    }
    /// <summary>画像コンポーネントのテクスチャ素材を走査します。</summary>
    template <class Visitor> void ComponentAssetReferences(ImageComponent& component, Visitor&& visitor)
    {
        visitor(component.texture);
    }
    /// <summary>シーン操作を行うボタンの遷移先素材を走査します。</summary>
    template <class Visitor> void ComponentAssetReferences(ButtonComponent& component, Visitor&& visitor)
    {
        if (component.action == "loadScene" || component.action == "loadSceneAdditive" ||
            component.action == "unloadScene")
        {
            visitor(component.target);
        }
    }
    /// <summary>地形のテクスチャ素材を走査します。</summary>
    template <class Visitor> void ComponentAssetReferences(TerrainComponent& component, Visitor&& visitor)
    {
        visitor(component.texture);
    }
    /// <summary>タイルマップのアトラス素材を走査します。</summary>
    template <class Visitor> void ComponentAssetReferences(TilemapComponent& component, Visitor&& visitor)
    {
        visitor(component.texture);
    }
    /// <summary>音源の音声素材を走査します。</summary>
    template <class Visitor> void ComponentAssetReferences(AudioSourceComponent& component, Visitor&& visitor)
    {
        visitor(component.clip);
    }
}
