#include "AudioPreview.h"

namespace Editor
{
    auto AudioPreview::Process(const std::filesystem::path& root) -> void
    {
        if (!pending)
            return;
        auto request = std::move(*pending);
        pending.reset();
        audio_.Shutdown();
        error.clear();
        if (request.clip.empty())
            return;
        if (!audio_.Initialize())
        {
            error = "音声出力を利用できません";
            return;
        }
        const auto sound = audio_.Load(root / request.clip);
        if (!sound)
        {
            error = "音声アセットをデコードできません";
            return;
        }
        audio_.SetVolume(sound, request.volume);
        audio_.Play(sound, request.loop);
        error.clear();
    }
}
