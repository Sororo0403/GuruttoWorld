#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace Engine
{
    struct RenderFrameContext
    {
        static constexpr unsigned int SlotCount = 2;
        unsigned int slot = 0;
        std::uint64_t serial = 0;
        std::vector<std::shared_ptr<const void>>* resources = nullptr;
        /// <summary>フェンス待機済みのフレーム枠をコマンドリストへ記録します。</summary>
        void Record(ID3D12GraphicsCommandList* commands) const
        {
            commands->SetPrivateData(PredicateKey, 0, nullptr);
            commands->SetPrivateData(Key, sizeof(*this), this);
        }
        /// <summary>出力先の切替に影響されない現在のフレーム枠を取得します。</summary>
        static bool Current(ID3D12GraphicsCommandList* commands, RenderFrameContext& result);
        /// <summary>記録を始める前に、資源をこのフレームの完了フェンスまで保持します。</summary>
        static bool Retain(ID3D12GraphicsCommandList* commands, std::shared_ptr<const void> resource);
        inline static constexpr GUID PredicateKey{
            0x5072ed88, 0x4675, 0x41e9, {0x86, 0xaf, 0x41, 0x4f, 0x0e, 0x7a, 0x58, 0x21}};
        inline static constexpr GUID Key{0xa848e282, 0x5891, 0x487a, {0x9f, 0xa7, 0x85, 0xc3, 0xed, 0x67, 0x73, 0x10}};
    };
}
