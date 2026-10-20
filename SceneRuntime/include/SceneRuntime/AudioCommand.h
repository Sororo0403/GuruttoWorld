#pragma once
#include <stdexcept>
#include <string>

namespace SceneRuntime
{
    enum class AudioCommandKind
    {
        Play,
        Cue
    };
    struct AudioCommand
    {
        AudioCommandKind kind = AudioCommandKind::Play;
        std::string target;
        bool operator==(const AudioCommand&) const = default;
        /// <summary>成功後に実行する音声指示の種類と対象名を検証します。</summary>
        void Validate() const
        {
            if ((kind != AudioCommandKind::Play && kind != AudioCommandKind::Cue) || target.empty() ||
                target.size() > 128 || target.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid audio command");
        }
    };
}
