#pragma once
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstring>

namespace Engine
{
    /// <summary>行列の全成分が有限値かを確認します。</summary>
    bool FiniteMatrix(const DirectX::XMFLOAT4X4& matrix);
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
