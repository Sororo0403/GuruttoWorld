#pragma once
#include <d3d12.h>
#include <cstdint>

namespace Engine
{
    struct RenderFrameContext
    {
        static constexpr unsigned int SlotCount=2;
        unsigned int slot=0;
        std::uint64_t serial=0;
        /// <summary>フェンス待機済みのフレーム枠をコマンドリストへ記録します。</summary>
        void Record(ID3D12GraphicsCommandList* commands) const { commands->SetPrivateData(Key,sizeof(*this),this); }
        /// <summary>出力先の切替に影響されない現在のフレーム枠を取得します。</summary>
        static bool Current(ID3D12GraphicsCommandList* commands,RenderFrameContext& result)
        {
            UINT bytes=sizeof(result);
            return commands && SUCCEEDED(commands->GetPrivateData(Key,&bytes,&result)) && bytes==sizeof(result) && result.slot<SlotCount;
        }
        inline static constexpr GUID Key{0xa848e282,0x5891,0x487a,{0x9f,0xa7,0x85,0xc3,0xed,0x67,0x73,0x10}};
    };
}
