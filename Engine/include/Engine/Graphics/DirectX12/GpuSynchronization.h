#pragma once

#include <Engine/Platform/Window.h>
#include <d3d12.h>

namespace Engine
{
    /// <summary>
    /// GPU キューへ完了値を登録します。失敗時はデバイス喪失を確定させ、安全な解放を可能にします。
    /// </summary>
    /// <param name="device">対象デバイス。</param>
    /// <param name="queue">処理を送信したキュー。</param>
    /// <param name="fence">完了値を記録するフェンス。</param>
    /// <param name="value">登録する完了値。</param>
    /// <returns>登録に成功した場合は true。false の場合はデバイスを再利用できません。</returns>
    bool SignalGpuFence(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64 value);

    /// <summary>
    /// 完了値まで待機します。イベント異常時はポーリングし、制限時間超過時はデバイス喪失を確定します。
    /// </summary>
    /// <param name="device">対象デバイス。</param>
    /// <param name="fence">待機するフェンス。</param>
    /// <param name="value">到達を待つ完了値。</param>
    /// <param name="event">待機イベント。nullptr の場合はポーリングします。</param>
    /// <param name="timeoutMilliseconds">待機の制限時間。</param>
    /// <returns>GPU 処理が正常に完了した場合は true。false の場合はデバイスを再利用できません。</returns>
    bool WaitForGpuFence(ID3D12Device* device, ID3D12Fence* fence, UINT64 value,
        HANDLE event, DWORD timeoutMilliseconds = 30000);
}
