#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>

namespace Editor
{
    class SceneSnapshotCache final
    {
    public:
        /// <summary>編集ドキュメントだけを基準に保存結果を再利用します。</summary>
        const std::string& Get(const SceneRuntime::SceneLayout& layout);
        /// <summary>実際に直列化した回数を返します。</summary>
        size_t Serializations() const
        {
            return serializations_;
        }

    private:
        SceneRuntime::SceneLayout saved_;
        std::string json_;
        bool valid_ = false;
        size_t serializations_ = 0;
    };
}
