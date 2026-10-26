#include <Engine/Animation/SkinMatrix.h>

namespace Engine
{
    bool FiniteMatrix(const DirectX::XMFLOAT4X4& matrix)
    {
        return std::all_of(std::begin(matrix.m), std::end(matrix.m),
                           [](const auto& row)
                           {
                               return std::all_of(std::begin(row), std::end(row),
                                                  [](float entry)
                                                  {
                                                      return std::isfinite(entry);
                                                  });
                           });
    }
}
