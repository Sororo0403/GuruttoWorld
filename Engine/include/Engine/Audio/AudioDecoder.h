#pragma once
#include <Engine/Audio/WaveData.h>

namespace Engine
{
    /// <summary>
    /// Media Foundation で音声を PCM にデコードします。呼び出し元で COM と MFStartup を初期化してください。
    /// OS のデコーダーが対応する形式を扱います。モノラル・ステレオ、展開後 64 MiB までです。
    /// 拡張形式の整数 PCM は有効ビット数と通常のチャンネル配置を検証し、格納幅の PCM に正規化します。
    /// 失敗時は false を返し、出力を変更しません。
    /// </summary>
    bool DecodeAudioFile(const std::filesystem::path& path, WaveData& wave);
}
