#pragma once
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstring>

namespace Engine
{
    inline bool FiniteMatrix(const DirectX::XMFLOAT4X4& matrix)
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
    struct SkinMatrix
    {
        DirectX::XMFLOAT4X4 position{}, normal{};
        /// <summary>アップロードする行列の全要素を比較します。</summary>
        bool operator==(const SkinMatrix& other) const
        {
            return std::memcmp(this, &other, sizeof(*this)) == 0;
        }
    };
    static_assert(sizeof(SkinMatrix) == 128);
}
