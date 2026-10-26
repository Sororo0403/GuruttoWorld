#pragma once
#include <SceneRuntime/Rendering/MaterialAssetFormat.h>
#include <Engine/Assets/AssetReferenceResolver.h>
#include <Engine/Core/Validation.h>
#include <array>
#include <iterator>
#include <Engine/Core/Profiler.h>
#include <Engine/Graphics/Materials/Material.h>
#include <Engine/Graphics/Renderers/MeshResources.h>
#include <SceneRuntime/Rendering/MaterialTextureCache.h>
#include <Engine/Core/Json.h>
#include <Engine/Assets/AssetDatabase.h>
#include <fstream>
#include <Engine/Platform/Window.h>
#include <filesystem>
#include <cmath>
#include <algorithm>

namespace SceneRuntime
{
    struct MaterialAsset
    {
        Engine::Material values;
        std::filesystem::path texture;
        std::filesystem::path normalTexture;
        std::filesystem::path environmentTexture, lightmap;
        std::filesystem::path shader;
        /// <summary>省略可能な汎用シェーダー値を、4 成分の配列として読み込みます。</summary>
        static std::array<float, 4> ReadShaderParameters(const Engine::Json& source)
        {
            return MaterialAssetFormat::ReadShaderParameters(source);
        }

        /// <summary>保存形式と同じ規則でマテリアルの論理パスを検証します。</summary>
        static bool ValidPath(const std::filesystem::path& path)
        {
            return MaterialAssetFormat::ValidPath(path);
        }

        /// <summary>テクスチャの参照と利用条件を検証します。</summary>
        void ValidateTextures() const;
        /// <summary>設定の整合性を検証します。</summary>
        void Validate() const;
        /// <summary>保存内容を読み込みます。</summary>
        static MaterialAsset Load(const std::filesystem::path& root, const std::filesystem::path& path,
                                  const Engine::AssetDatabase* assetIndex = nullptr);
        /// <summary>素材の保存内容を、副作用なしに構築して検証します。</summary>
        std::string SerializeForSave(const std::filesystem::path& root, const std::filesystem::path& path) const;
        /// <summary>保存内容を検証してから、共通の原子的な書き込みで確定します。</summary>
        void Save(const std::filesystem::path& root, const std::filesystem::path& path) const;
        /// <summary>使用する資源と状態を準備します。</summary>
        std::shared_ptr<const Engine::Material> Prepare(
            ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& root,
            std::shared_ptr<const Engine::ContentContext> context = {}) const;

    private:
        /// <summary>マテリアルが所有するシェーダー・テクスチャ素材だけを解決します。</summary>
        static void References(Engine::Json& json, const Engine::AssetDatabase& index);
    };
}
