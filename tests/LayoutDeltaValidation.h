#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <SceneRuntime/SceneTransforms.h>
#include <limits>
#include <cmath>
#include <stdexcept>

namespace LayoutDeltaValidation
{
    /// <summary>親変更の全解決と姿勢差分の子伝搬が通常解決と一致し、不正な更新では出力を保持することを確認します。</summary>
    inline void Transforms()
    {
        using namespace SceneRuntime;
        SceneLayout before;
        ScenePlacement root;
        root.id = "root";
        root.scale = {2, 1, 3};
        root.rotation = {.1f, .4f, .2f};
        ScenePlacement child;
        child.id = "child";
        child.parentId = "root";
        child.position = {1, 2, 3};
        child.rotation = {.2f, .3f, .4f};
        ScenePlacement grandchild;
        grandchild.id = "grandchild";
        grandchild.parentId = "child";
        grandchild.position = {1, 0, 2};
        ScenePlacement independent;
        independent.id = "independent";
        // Parents deliberately follow their children in layout order.
        before.objects = {grandchild, child, independent, root};
        before.Validate();
        std::vector<DirectX::XMFLOAT4X4> previous;
        std::string error;
        if (!SceneTransforms::Resolve(before, previous, error))
            throw std::runtime_error(error);
        const auto equal = [](const auto& left, const auto& right)
        {
            if (left.size() != right.size())
                return false;
            for (size_t index = 0; index < left.size(); ++index)
                if (!SceneTransforms::Matches(left[index], right[index]))
                    return false;
            return true;
        };
        for (int variant = 0; variant < 12; ++variant)
        {
            auto changed = before;
            switch (variant)
            {
            case 0:
                break;
            case 1:
                changed.objects[3].position[0] = 4;
                break;
            case 2:
                changed.objects[1].rotation[2] = .8f;
                break;
            case 3:
                changed.objects[0].scale[1] = -2;
                break;
            case 4:
                changed.objects[1].parentId = "independent";
                break;
            case 5:
                changed.objects[3].position[0] = std::numeric_limits<float>::quiet_NaN();
                break;
            case 6:
                changed.objects[0].scale[1] = 0;
                break;
            case 7:
                changed.objects[3].parentId = "grandchild";
                break;
            case 8:
                changed.objects[0].parentId = "missing";
                break;
            case 9:
                changed.objects[0].id = "child";
                break;
            case 10:
                changed.objects.erase(changed.objects.begin());
                break;
            case 11:
                changed.objects[3].scale[0] = (std::numeric_limits<float>::max)();
                break;
            }
            auto full = previous, incremental = previous;
            const bool valid = SceneTransforms::Resolve(changed, full, error);
            const bool deltaValid = SceneTransforms::ResolveChanges(changed, before, previous, incremental, error);
            if (valid != deltaValid || !equal(full, incremental))
                throw std::runtime_error("Full and incremental transform resolution must agree");
            if (!valid && !equal(incremental, previous))
                throw std::runtime_error("Failed incremental transform resolution must preserve output");
        }
    }

    /// <summary>差分検証が成分と配置全体の不正な変更を従来と同じ規則で拒否することを確認します。</summary>
    inline void Run()
    {
        Transforms();
        using namespace SceneRuntime;
        SceneLayout baseline;
        ScenePlacement camera;
        camera.id = "camera";
        camera.camera.emplace();
        ScenePlacement player;
        player.id = "player";
        player.playerController.emplace();
        ScenePlacement child;
        child.id = "child";
        child.parentId = "player";
        baseline.objects = {camera, player, child};
        baseline.settings.mainCamera = "camera";
        for (int index = 0; index < 33; ++index)
        {
            ScenePlacement light;
            light.id = "light-" + std::to_string(index);
            light.pointLight.emplace();
            light.pointLight->enabled = index < 32;
            baseline.objects.push_back(std::move(light));
        }
        baseline.Validate();
        const auto accepted = [](const SceneLayout& layout, const SceneLayout* before)
        {
            try
            {
                if (before)
                    layout.ValidateChanges(*before);
                else
                    layout.Validate();
                return true;
            }
            catch (const std::exception&)
            {
                return false;
            }
        };
        const auto check = [&](const SceneLayout& changed, bool expected)
        {
            if (accepted(changed, nullptr) != expected || accepted(changed, &baseline) != expected)
                throw std::runtime_error("Full and incremental layout validation must agree");
        };
        check(baseline, true);
        auto changed = baseline;
        changed.objects[1].position[0] = 2;
        changed.objects[2].parentId = "camera";
        check(changed, true);
        changed = baseline;
        changed.objects[1].playerController->moveSpeed = 3;
        check(changed, true);
        for (int variant = 0; variant < 16; ++variant)
        {
            changed = baseline;
            switch (variant)
            {
            case 0:
                changed.objects[1].position[0] = std::numeric_limits<float>::quiet_NaN();
                break;
            case 1:
                changed.objects[1].scale[0] = 0;
                break;
            case 2:
                changed.objects[1].rotation[0] = std::numeric_limits<float>::infinity();
                break;
            case 3:
                changed.objects[1].parentId = "missing";
                break;
            case 4:
                changed.objects[1].parentId = "child";
                break;
            case 5:
                changed.objects[1].id = "camera";
                break;
            case 6:
                changed.objects[1].playerController->moveSpeed = -1;
                break;
            case 7:
                changed.objects[1].playerController->id = "transform";
                break;
            case 8:
                changed.settings.mainCamera = "player";
                break;
            case 9:
                changed.settings.background[0] = std::numeric_limits<float>::quiet_NaN();
                break;
            case 10:
                changed.objects.back().pointLight->enabled = true;
                break;
            case 11:
                changed.objects[0].camera.reset();
                break;
            case 12:
                changed.assetReferences["../escape"] = "invalid-id";
                break;
            case 13:
                changed.objects[1].prefab = PrefabLink{};
                break;
            case 14:
                changed.objects[1].parentId = "player";
                break;
            case 15:
                changed.objects[1].id.clear();
                break;
            }
            check(changed, false);
        }
        for (const float sign : {-1.0f, 1.0f})
        {
            changed = baseline;
            changed.objects[1].scale[0] = sign * 0.000001f;
            check(changed, true);
            changed.objects[1].scale[0] = sign * std::nextafter(0.000001f, 0.0f);
            check(changed, false);
            changed.objects[1].scale[0] = sign * std::nextafter(0.000001f, 1.0f);
            check(changed, true);
        }
        changed = baseline;
        changed.objects.pop_back();
        check(changed, true);
        changed = baseline;
        ScenePlacement added;
        added.id = "added";
        changed.objects.push_back(added);
        check(changed, true);
        changed.objects.back().scale[0] = 0;
        check(changed, false);
        changed = baseline;
        std::swap(changed.objects[0], changed.objects[1]);
        check(changed, true);
    }
}
