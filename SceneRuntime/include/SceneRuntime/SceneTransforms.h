#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <DirectXMath.h>

namespace SceneRuntime
{
    // All outputs are preserved on failure. Full matrices retain shear from nonuniform parent scales.
    class SceneTransforms final
    {
    public:
        static bool Compose(const ScenePlacement& placement, DirectX::XMFLOAT4X4& output);
        static bool WorldToLocal(const DirectX::XMFLOAT4X4& world,
            const DirectX::XMFLOAT4X4& parentWorld, DirectX::XMFLOAT4X4& output);
        // Resolve parent-relative SRT into world matrices, in layout.objects order.
        static bool Resolve(const SceneLayout& layout,
            std::vector<DirectX::XMFLOAT4X4>& output, std::string& error);
        // Decompose an affine matrix into editable SRT; preserve output on failure and reject shear.
        static bool ReadTransform(const DirectX::XMFLOAT4X4& matrix, const ScenePlacement& reference, ScenePlacement& output);
        static bool Matches(const DirectX::XMFLOAT4X4& left, const DirectX::XMFLOAT4X4& right);
        static bool IsUsable(const DirectX::XMFLOAT4X4& matrix);
    };
}
