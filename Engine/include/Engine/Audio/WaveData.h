#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmreg.h>
#include <filesystem>
#include <vector>

namespace Engine
{
    struct WaveData
    {
        WAVEFORMATEX format{};
        std::vector<unsigned char> samples;
    };

    /// <summary>
    /// Media Foundation の共通デコーダーで WAV を PCM に読み込む互換用の入口です。
    /// COM と Media Foundation は内部で初期化するため、呼び出し元の初期化は不要です。
    /// モノラル・ステレオ、8/16/24/32 bit を扱い、失敗時は出力を変更しません。
    /// </summary>
    /// <param name="path">読み込む WAV ファイル。</param>
    /// <param name="wave">成功時のみ置き換える音声データ。</param>
    /// <returns>読み込みに成功した場合は true。入力と展開後の最大サイズはそれぞれ 64 MiB。</returns>
    bool LoadWaveFile(const std::filesystem::path& path, WaveData& wave);
}
