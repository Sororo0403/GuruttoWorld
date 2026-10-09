#pragma once
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <type_traits>
#include <vector>

namespace SceneRuntime
{
    struct ScriptObjectReference
    {
        std::string id;
        bool operator==(const ScriptObjectReference&) const = default;
    };
    struct ScriptValue
    {
        using Array=std::vector<ScriptValue>;
        using Object=std::map<std::string,ScriptValue>;
        std::variant<float,bool,std::string,ScriptObjectReference,Array,Object> value=0.0f;
        bool operator==(const ScriptValue&) const = default;

        /// <summary>データの深さ・総要素数・値を検証します。不正な値は例外で拒否します。</summary>
        void Validate(size_t depth,size_t& remaining) const
        {
            if (depth>8 || remaining==0) throw std::runtime_error("Script data exceeds nesting or element limit");
            --remaining;
            std::visit([&](const auto& item) { ValidateItem(item,depth,remaining); },value);
        }

        /// <summary>複製された階層内のオブジェクト参照を新しいIDへ置き換えます。</summary>
        template<class IdMap> void Remap(const IdMap& ids)
        {
            std::visit([&](auto& item) {
                using T=std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T,ScriptObjectReference>) {
                    if (const auto found=ids.find(item.id);found!=ids.end()) item.id=found->second;
                } else if constexpr (std::is_same_v<T,Array>) {
                    for (auto& child:item) child.Remap(ids);
                } else if constexpr (std::is_same_v<T,Object>) {
                    for (auto& [name,child]:item) { static_cast<void>(name); child.Remap(ids); }
                }
            },value);
        }
    private:
        /// <summary>数値の有限性と保存範囲を検証します。</summary>
        static void ValidateItem(float item,size_t,size_t&)
        { if (!std::isfinite(item) || std::abs(item)>1000000) throw std::runtime_error("Invalid script data number"); }
        /// <summary>真偽値は常に有効です。</summary>
        static void ValidateItem(bool,size_t,size_t&) {}
        /// <summary>文字列の保存範囲を検証します。</summary>
        static void ValidateItem(const std::string& item,size_t,size_t&)
        { if (item.size()>4096 || item.find('\0')!=std::string::npos) throw std::runtime_error("Invalid script data string"); }
        /// <summary>オブジェクト参照IDを検証します。</summary>
        static void ValidateItem(const ScriptObjectReference& item,size_t,size_t&)
        { if (item.id.size()>128 || item.id.find('\0')!=std::string::npos) throw std::runtime_error("Invalid script object reference"); }
        /// <summary>配列の要素数と子データを検証します。</summary>
        static void ValidateItem(const Array& item,size_t depth,size_t& remaining)
        {
            if (item.size()>256) throw std::runtime_error("Script data array is too large");
            for (const auto& child:item) child.Validate(depth+1,remaining);
        }
        /// <summary>構造のフィールド名と子データを検証します。</summary>
        static void ValidateItem(const Object& item,size_t depth,size_t& remaining)
        {
            if (item.size()>64) throw std::runtime_error("Script data object is too large");
            for (const auto& [name,child]:item) {
                if (name.empty() || name.size()>128 || name.find('\0')!=std::string::npos) throw std::runtime_error("Invalid script data field name");
                child.Validate(depth+1,remaining);
            }
        }
    };
}
