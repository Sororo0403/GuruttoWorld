#pragma once
#include <Engine/Audio/AudioSystem.h>
#include <optional>
namespace Editor
{
    class AudioPreview final
    {
    public:
        struct Request
        {
            std::filesystem::path clip;
            float volume = 1;
            bool loop = false;
        };
        static inline std::optional<Request> pending;
        static void Play(std::filesystem::path clip, float volume = 1, bool loop = false)
        {
            pending = Request{std::move(clip), volume, loop};
        }
        static void StopRequest()
        {
            pending = Request{};
        }
        /// <summary>保留中の要求を処理します。</summary>
        void Process(const std::filesystem::path& root);
        std::string error;

    private:
        Engine::AudioSystem audio_;
    };
}
