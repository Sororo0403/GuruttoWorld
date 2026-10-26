#pragma once
#include <d3d12.h>

namespace Engine
{
    struct RenderTargetBinding
    {
        D3D12_CPU_DESCRIPTOR_HANDLE color{}, depth{};
        D3D12_VIEWPORT viewport{};
        D3D12_RECT scissor{};
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        /// <summary>出力先とビューポートを設定し、補助パスから復元できるよう記録します。</summary>
        void Bind(ID3D12GraphicsCommandList* commands) const;
        /// <summary>現在記録中の出力先を取得します。登録されていなければ失敗します。</summary>
        static bool Current(ID3D12GraphicsCommandList* commands, RenderTargetBinding& binding)
        {
            UINT bytes = sizeof(binding);
            return commands && SUCCEEDED(commands->GetPrivateData(Key, &bytes, &binding)) && bytes == sizeof(binding);
        }
        inline static constexpr GUID Key{0x51ae4d68, 0x8be0, 0x4c62, {0xb5, 0x25, 0x58, 0x01, 0x4f, 0x2a, 0x98, 0x32}};
    };
}
