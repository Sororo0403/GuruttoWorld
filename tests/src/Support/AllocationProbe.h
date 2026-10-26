#pragma once
#include <cstddef>

namespace TestSupport
{
    struct AllocationCounts
    {
        size_t count = 0, bytes = 0;
    };

    /// <summary>検証EXEの現在のスレッドで、C++の確保回数と要求バイト数を計測します。</summary>
    class AllocationProbe final
    {
    public:
        /// <summary>確保の計測を開始します。denyを指定すると確保要求を失敗させます。</summary>
        explicit AllocationProbe(bool deny = false) noexcept;
        /// <summary>計測前のスレッド設定へ戻します。</summary>
        ~AllocationProbe();
        /// <summary>計測範囲の所有権を複製しません。</summary>
        AllocationProbe(const AllocationProbe&) = delete;
        /// <summary>計測範囲の所有権を代入しません。</summary>
        AllocationProbe& operator=(const AllocationProbe&) = delete;
        /// <summary>この計測範囲で受け付けた要求数と要求バイト数を返します。</summary>
        AllocationCounts Counts() const noexcept;

    private:
        bool previousEnabled_, previousDenied_;
        AllocationCounts previousCounts_;
    };
}
