#pragma once
#include <cstdint>
#include <memory>

namespace SceneRuntime
{
    struct WorldIdentity;
    /// <summary>World・格納位置・発行世代を識別し、保存用ObjectIdと区別します。</summary>
    struct EntityHandle
    {
        std::shared_ptr<const WorldIdentity> world;
        std::uint64_t generation = 0;
        std::uint32_t slot = 0;
        /// <summary>未設定ではないハンドルかを返します。生存確認はWorldの読取ビューで行います。</summary>
        explicit operator bool() const noexcept
        {
            return world && generation != 0;
        }
        /// <summary>Worldと発行世代を含めて比較します。</summary>
        bool operator==(const EntityHandle&) const = default;
    };

    /// <summary>個体とコンポーネントの発行世代を識別し、削除後の同じ保存IDの再利用を区別します。</summary>
    struct ComponentInstanceHandle
    {
        EntityHandle entity;
        std::uint64_t generation = 0;
        /// <summary>個体とコンポーネントの世代を比較します。</summary>
        bool operator==(const ComponentInstanceHandle&) const = default;
    };
}
