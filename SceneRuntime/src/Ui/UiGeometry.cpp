#include "UiGeometryFunctions.h"

namespace SceneRuntime
{
    bool UiRect::Contains(float x, float y) const
    {
        if (x < clip[0] || y < clip[1] || x > clip[2] || y > clip[3])
            return false;
        x -= position[0] + size[0] * 0.5f;
        y -= position[1] + size[1] * 0.5f;
        const float a = x * std::cos(rotation) + y * std::sin(rotation),
                    b = -x * std::sin(rotation) + y * std::cos(rotation);
        return visible && opacity > 0.001f && size[0] > 0 && size[1] > 0 && std::abs(a) <= size[0] * 0.5f &&
               std::abs(b) <= size[1] * 0.5f;
    }
    UiState UiGeometry::Defaults(const UiPacket& packet)
    {
        return UiGeometryDetail::DefaultState(packet);
    }
    UiRect UiGeometry::Resolve(const UiPacket& packet, const UiNode& node, unsigned int width, unsigned int height,
                               const UiState& state)
    {
        return UiGeometryDetail::Resolve(packet, node, width, height, state);
    }
    std::string UiGeometry::InputText(const UiNode& node, const UiState& state)
    {
        return UiGeometryDetail::InputText(node, state);
    }
}
