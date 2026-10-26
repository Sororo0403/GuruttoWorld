#pragma once
#include <Engine/Core/Json.h>
#include <array>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace SceneRuntime
{
    struct MaterialAssetFormat final
    {
        /// <summary>保存された汎用シェーダー値を検証して読み取ります。</summary>
        static std::array<float, 4> ReadShaderParameters(const Engine::Json& source);
        /// <summary>マテリアルの論理パスを検証します。</summary>
        static bool ValidPath(const std::filesystem::path& path);
    };
}
