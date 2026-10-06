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
        // Matrix order follows layout.objects. World is the existing JSON's coordinate convention.
        static bool Resolve(const SceneLayout& layout, TransformSpace space,
            std::vector<DirectX::XMFLOAT4X4>& output, std::string& error);
        // Convert legacy world SRT to local SRT without moving objects. Refuse unrepresentable shear.
        static bool ConvertToLocal(const SceneLayout& source, SceneLayout& output, std::string& error);
        static bool IsUsable(const DirectX::XMFLOAT4X4& matrix);
    };
}
