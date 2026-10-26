#pragma once
#include <Engine/Core/Validation.h>
#include <array>
#include <string>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace SceneRuntime
{
    struct JointComponent
    {
        std::string id = "joint", type = "fixed", target;
        bool enabled = true, collideConnected = false;
        std::array<float, 3> anchor{}, connectedAnchor{}, axis{0, 0, 1};
        float minimum = -90, maximum = 90, swing = 45, minDistance = 0, maxDistance = 1;
        bool operator==(const JointComponent&) const = default;
        /// <summary>有限なアンカー、軸、距離と角度制限を検証します。</summary>
        void Validate() const;

    private:
        /// <summary>角度と距離の制限値を検証します。</summary>
        void ValidateLimits() const;
    };
}
