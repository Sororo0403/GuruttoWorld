#include <Engine/Core/BuildIdentity.h>
#include "EngineBuildIdentity.generated.h"

namespace Engine
{
    std::uint64_t BuildIdentity()
    {
        return EngineBuildFingerprint;
    }
    std::filesystem::path BuildSourceRoot()
    {
        return EngineBuildSourceRoot;
    }
}
