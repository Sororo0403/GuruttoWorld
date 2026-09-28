#pragma once

#include <Engine/Platform/Window.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <filesystem>

namespace Engine
{
    /// <summary>
    /// HLSL をコンパイルします。警告もエラーとし、Debug ではデバッグ情報を生成します。
    /// </summary>
    /// <param name="path">HLSL ファイル。</param>
    /// <param name="entry">エントリーポイント名。</param>
    /// <param name="target">シェーダーモデル。</param>
    /// <param name="shader">コンパイル結果の出力先。</param>
    /// <returns>コンパイルに成功した場合は true。</returns>
    bool CompileShader(const std::filesystem::path& path, const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& shader);
}
