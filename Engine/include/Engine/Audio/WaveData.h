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
    /// 非圧縮 PCM WAV（モノラル・ステレオ、8/16/24/32 bit）を読み込み、チャンク境界と形式を検証します。
    /// </summary>
    /// <param name="path">読み込む WAV ファイル。</param>
    /// <param name="wave">成功時のみ置き換える音声データ。</param>
    /// <returns>読み込みに成功した場合は true。最大ファイルサイズは 64 MiB。</returns>
    bool LoadWaveFile(const std::filesystem::path& path, WaveData& wave);
}
