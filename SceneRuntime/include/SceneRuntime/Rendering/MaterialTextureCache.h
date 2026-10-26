#pragma once
#include <Engine/Graphics/Resources/Texture2D.h>
#include <Engine/Assets/AssetSourceStamp.h>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace SceneRuntime
{
    class MaterialTextureCache final
    {
    public:
        /// <summary>同じ描画所有者の画像を共有し、画像とメタデータの変更時だけ転送し直します。</summary>
        static std::shared_ptr<const Engine::Texture2D> Load(ID3D12Device* device, ID3D12CommandQueue* queue,
                                                             const std::filesystem::path& path);
    };
}
