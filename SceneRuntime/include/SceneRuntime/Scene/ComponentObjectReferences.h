#pragma once
#include <SceneRuntime/Scene/SceneComponents.h>
#include <SceneRuntime/Gameplay/GenreComponents.h>
#include <SceneRuntime/Physics/JointComponent.h>
#include <SceneRuntime/Ui/UiComponents.h>
#include <SceneRuntime/Physics/Ragdoll.h>

namespace SceneRuntime
{
    /// <summary>スクリプトの型付き参照を、配列・構造の内部まで走査します。</summary>
    template <class Visitor> void ComponentObjectReferences(ScriptComponent& component, Visitor&& visitor)
    {
        component.VisitObjectReferences(visitor);
    }
    /// <summary>Jointの相手個体を走査します。</summary>
    template <class Visitor> void ComponentObjectReferences(JointComponent& component, Visitor&& visitor)
    {
        visitor(component.target);
    }
    /// <summary>NavAgentのNavMesh個体を走査します。</summary>
    template <class Visitor> void ComponentObjectReferences(NavAgentComponent& component, Visitor&& visitor)
    {
        component.VisitObjectReferences(visitor);
    }
    /// <summary>Ragdollの骨に対応する剛体個体を走査します。</summary>
    template <class Visitor> void ComponentObjectReferences(RagdollComponent& component, Visitor&& visitor)
    {
        component.VisitObjectReferences(visitor);
    }
    /// <summary>ボタンの個体参照を走査し、シーンパスと状態名は保持します。</summary>
    template <class Visitor> void ComponentObjectReferences(ButtonComponent& component, Visitor&& visitor)
    {
        if (component.action != "loadScene" && component.action != "loadSceneAdditive" &&
            component.action != "unloadScene" && component.action != "setState")
        {
            visitor(component.target);
        }
        visitor(component.sound);
    }
    /// <summary>メニュー遷移のカメラ個体を走査します。</summary>
    template <class Visitor> void ComponentObjectReferences(CanvasComponent& component, Visitor&& visitor)
    {
        if (!component.menu)
        {
            return;
        }
        component.menu->VisitObjectReferences(visitor);
    }
}
