#pragma once
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace TransformResolutionCacheValidation
{
    /// <summary>キャッシュの結果と失敗時の出力を、毎回計算する経路と比較します。</summary>
    inline void Run()
    {
        using namespace SceneRuntime;
        const auto check = [](const SceneLayout& layout)
        {
            std::vector<DirectX::XMFLOAT4X4> cached, calculated;
            std::string cacheError, calculationError;
            const bool expected = SceneTransforms::Resolve(layout, {}, calculated, calculationError);
            for (int repeat = 0; repeat < 2; ++repeat)
            {
                const bool actual = SceneTransforms::Resolve(layout, cached, cacheError);
                if (actual != expected || cacheError != calculationError || cached.size() != calculated.size() ||
                    (!cached.empty() &&
                     std::memcmp(cached.data(), calculated.data(), cached.size() * sizeof(cached.front())) != 0))
                    throw std::runtime_error("transform cache differs from full resolution");
            }
        };
        SceneLayout layout;
        ScenePlacement parent, child;
        parent.id = "cache-parent";
        parent.position = {2, 3, 4};
        child.id = "cache-child";
        child.parentId = parent.id;
        child.position = {1, 0, 0};
        layout.objects = {child, parent};
        check(layout);
        for (int index = 0; index < 8; ++index)
        {
            layout.objects[1].position[0] += 1;
            layout.objects[1].rotation[1] += .1f;
            layout.objects[1].scale[0] += .1f;
            check(layout);
        }
        auto invalid = layout;
        invalid.objects[1].id = child.id;
        check(invalid);
        invalid = layout;
        invalid.objects[1].parentId = child.id;
        check(invalid);
        invalid = layout;
        invalid.objects[0].parentId = "missing";
        check(invalid);
        invalid = layout;
        invalid.objects[0].scale[0] = 0;
        check(invalid);
        invalid = layout;
        invalid.objects[0].position[0] = std::numeric_limits<float>::quiet_NaN();
        check(invalid);
        check(layout);
        std::swap(layout.objects[0], layout.objects[1]);
        check(layout);
        layout.objects[1].parentId.clear();
        check(layout);
        std::vector<DirectX::XMFLOAT4X4> modified;
        std::string error;
        if (!SceneTransforms::Resolve(layout, modified, error))
            throw std::runtime_error(error);
        modified.front()._41 = -999;
        check(layout);
        check(SceneLayout{});
    }
}
