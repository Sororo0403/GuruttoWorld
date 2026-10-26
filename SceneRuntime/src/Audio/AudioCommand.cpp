#include <SceneRuntime/Audio/AudioCommand.h>

namespace SceneRuntime
{
    auto AudioCommand::Validate() const -> void
    {
        if ((kind != AudioCommandKind::Play && kind != AudioCommandKind::Cue) || target.empty() ||
            target.size() > 128 || target.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid audio command");
    }
}
