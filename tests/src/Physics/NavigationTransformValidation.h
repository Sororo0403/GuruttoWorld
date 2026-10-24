#pragma once
#include <SceneRuntime/Gameplay/Navigation.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <algorithm>
#include <cmath>
namespace NavigationTransformValidation
{
    inline void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    inline SceneRuntime::ScenePlacement Mesh()
    {
        SceneRuntime::ScenePlacement p;
        p.id = "mesh";
        p.navMesh.emplace();
        p.navMesh->baked = true;
        return p;
    }
    inline SceneRuntime::SceneLayout Family(bool reversed)
    {
        SceneRuntime::ScenePlacement parent;
        parent.id = "parent";
        parent.position = {.5f, 0, .5f};
        parent.scale = {2, 1, 1};
        parent.rotation[1] = .3f;
        parent.navAgent.emplace();
        parent.navAgent->mesh = "mesh";
        parent.navAgent->speed = 1;
        parent.navAgent->destination = {5.5f, 0, .5f};
        auto child = parent;
        child.id = "child";
        child.parentId = "bridge";
        child.position = {0, 0, 0};
        child.scale = {1, 1, 1};
        child.rotation = {};
        SceneRuntime::ScenePlacement bridge;
        bridge.id = "bridge";
        bridge.parentId = "parent";
        SceneRuntime::ScenePlacement follower;
        follower.id = "follower";
        follower.parentId = "child";
        follower.position = {0, 1, 0};
        SceneRuntime::SceneLayout layout;
        layout.objects = {Mesh(), parent, bridge, child, follower};
        if (reversed)
            std::reverse(layout.objects.begin(), layout.objects.end());
        return layout;
    }
    inline void Hierarchy(bool reversed)
    {
        auto layout = Family(reversed);
        std::string error;
        Require(SceneRuntime::Navigation::Advance(layout, 1, error), "simultaneous parent and child movement");
        layout = SceneRuntime::SceneLayout::Parse(layout.Serialize());
        std::vector<DirectX::XMFLOAT4X4> matrices;
        Require(SceneRuntime::SceneTransforms::Resolve(layout, matrices, error),
                "navigation hierarchy resolves after roundtrip");
        for (size_t i = 0; i < layout.objects.size(); ++i)
        {
            if (layout.objects[i].id == "mesh")
                continue;
            Require(std::abs(matrices[i]._41 - 1.5f) < .001f,
                    "parent, nested agent and non-agent descendants move once independent of order");
        }
        auto invalid = Family(reversed);
        invalid.objects[0].parentId = "missing";
        const auto before = invalid.objects[0].position;
        Require(!SceneRuntime::Navigation::Advance(invalid, 1, error) && invalid.objects[0].position == before,
                "failed navigation preserves layout");
    }
    inline void ObstacleScale(float scale, float center, bool walkable)
    {
        auto mesh = Mesh();
        SceneRuntime::ScenePlacement box;
        box.id = "box";
        box.parentId = "box-parent";
        box.boxCollider.emplace();
        box.boxCollider->size = {1, 2, 1};
        SceneRuntime::ScenePlacement parent;
        parent.id = "box-parent";
        parent.position = {center, 1, .5f};
        parent.scale = {scale, 1, 1};
        SceneRuntime::SceneLayout layout;
        layout.objects = {mesh, box, parent};
        std::string error;
        Require(SceneRuntime::Navigation::Bake(layout, layout.objects[0], error), "scaled obstacle bake");
        const auto restored = SceneRuntime::SceneLayout::Parse(layout.Serialize());
        Require(restored.objects[0].navMesh->walkable[0] == walkable,
                "world agent radius respects scaled and mirrored obstacle parents");
    }
    inline void Slope(float scale, bool walkable)
    {
        auto mesh = Mesh();
        mesh.parentId = "terrain-parent";
        mesh.terrain.emplace();
        mesh.terrain->columns = 2;
        mesh.terrain->rows = 2;
        mesh.terrain->heights = {0, .75f, 0, .75f};
        SceneRuntime::ScenePlacement parent;
        parent.id = "terrain-parent";
        parent.scale = {1, scale, 1};
        SceneRuntime::SceneLayout layout;
        layout.objects = {mesh, parent};
        std::string error;
        Require(SceneRuntime::Navigation::Bake(layout, layout.objects[0], error), "scaled terrain bake");
        Require(layout.objects[0].navMesh->walkable[0] == walkable, "maximum slope uses world terrain tangents");
    }
    inline void Rotation()
    {
        auto mesh = Mesh();
        mesh.rotation[2] = DirectX::XM_PI / 3;
        SceneRuntime::SceneLayout layout;
        layout.objects = {mesh};
        std::string error;
        Require(SceneRuntime::Navigation::Bake(layout, layout.objects[0], error), "rotated navmesh bake");
        Require(!layout.objects[0].navMesh->walkable[0], "rotated flat navmesh exceeds world maximum slope");
    }
    inline void Run()
    {
        Hierarchy(false);
        Hierarchy(true);
        ObstacleScale(2, 1.8f, true);
        ObstacleScale(.5f, .9f, false);
        ObstacleScale(-2, 1.8f, true);
        Slope(1, true);
        Slope(2, false);
        Slope(.5f, true);
        Rotation();
    }
}
