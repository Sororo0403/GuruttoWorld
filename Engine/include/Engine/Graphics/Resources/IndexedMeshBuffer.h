#pragma once

#include <Engine/Platform/Window.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Engine
{
    /// <summary>
    /// 頂点と 32 ビットインデックスを DEFAULT ヒープへ転送し、完了を待って描画用ビューを返します。
    /// </summary>
    /// <param name="device">生成に使用するデバイス。</param>
    /// <param name="queue">転送に使用する DIRECT 型キュー。</param>
    /// <param name="vertices">頂点配列のバイト列。頂点サイズは 4 バイトの倍数で指定します。</param>
    /// <param name="vertexStride">一頂点のバイト数。</param>
    /// <param name="indices">頂点配列を参照するインデックス。</param>
    /// <param name="resource">転送済みリソースの出力先。空の状態で渡してください。</param>
    /// <param name="vertexView">頂点バッファービューの出力先。</param>
    /// <param name="indexView">インデックスバッファービューの出力先。</param>
    /// <returns>生成と転送に成功した場合は true。失敗時は出力を変更しません。</returns>
    bool CreateIndexedMeshBuffer(ID3D12Device* device, ID3D12CommandQueue* queue,
        std::span<const std::byte> vertices, UINT vertexStride, std::span<const std::uint32_t> indices,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
        D3D12_VERTEX_BUFFER_VIEW& vertexView, D3D12_INDEX_BUFFER_VIEW& indexView);
}
