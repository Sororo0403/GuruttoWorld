#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Engine::AssetBinary
{
    // 固定幅のCPU形式です。GPU資源とプロセス内ポインターは保存しません。
    class Writer final
    {
    public:
        std::string bytes;
        /// <summary>固定幅の値を保存します。</summary>
        template <class T> void Pod(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            bytes.append(reinterpret_cast<const char*>(&value), sizeof(T));
        }
        /// <summary>要素数を上限付きで保存します。</summary>
        void Count(size_t count)
        {
            if (count > static_cast<size_t>(UINT32_MAX))
                throw std::runtime_error("CPU artifact exceeds item limit");
            Pod(static_cast<std::uint32_t>(count));
        }
        /// <summary>長さ付きUTF-8を保存します。</summary>
        void Text(const std::string& value)
        {
            Count(value.size());
            bytes += value;
        }
        /// <summary>固定幅要素の配列を保存します。</summary>
        template <class T> void Array(const std::vector<T>& values)
        {
            Count(values.size());
            static_assert(std::is_trivially_copyable_v<T>);
            bytes.append(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(T));
        }
    };
    class Reader final
    {
        std::string_view bytes_;

    public:
        /// <summary>検証済み成果物の内容を借用します。</summary>
        explicit Reader(std::string_view bytes) : bytes_(bytes) {}
        /// <summary>残りの長さを確認して固定幅の値を読みます。</summary>
        template <class T> T Pod()
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if (bytes_.size() < sizeof(T))
                throw std::runtime_error("Truncated CPU artifact");
            T value;
            std::memcpy(&value, bytes_.data(), sizeof(T));
            bytes_.remove_prefix(sizeof(T));
            return value;
        }
        /// <summary>残りの長さと上限を満たす要素数を読みます。</summary>
        std::uint32_t Count()
        {
            const auto count = Pod<std::uint32_t>();
            if (count > bytes_.size())
                throw std::runtime_error("Invalid CPU artifact count");
            return count;
        }
        /// <summary>長さ付きUTF-8を読みます。</summary>
        std::string Text()
        {
            const auto count = Count();
            std::string result(bytes_.substr(0, count));
            bytes_.remove_prefix(count);
            return result;
        }
        /// <summary>配列全体の長さを確認して読みます。</summary>
        template <class T> std::vector<T> Array()
        {
            const auto count = Count();
            if (count > bytes_.size() / sizeof(T))
                throw std::runtime_error("Invalid CPU artifact array");
            static_assert(std::is_trivially_copyable_v<T>);
            std::vector<T> values(count);
            if (count)
                std::memcpy(values.data(), bytes_.data(), static_cast<size_t>(count) * sizeof(T));
            bytes_.remove_prefix(static_cast<size_t>(count) * sizeof(T));
            return values;
        }
        /// <summary>読み残した未定義の内容を拒否します。</summary>
        void End() const
        {
            if (!bytes_.empty())
                throw std::runtime_error("Trailing CPU artifact data");
        }
    };
}
