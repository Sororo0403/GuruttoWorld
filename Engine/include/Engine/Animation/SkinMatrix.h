#pragma once
#include <DirectXMath.h>
#include <cstring>

namespace Engine
{
    struct SkinMatrix
    {
        DirectX::XMFLOAT4X4 position{},normal{};
        /// <summary>アップロードする行列の全要素を比較します。</summary>
        bool operator==(const SkinMatrix& other) const { return std::memcmp(this,&other,sizeof(*this))==0; }
    };
    static_assert(sizeof(SkinMatrix)==128);
}
