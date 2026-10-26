#pragma once
#include <Engine/Core/Validation.h>
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
        using Array = std::vector<ScriptValue>;
        using Object = std::map<std::string, ScriptValue>;
        std::variant<float, bool, std::string, ScriptObjectReference, Array, Object> value = 0.0f;
        bool operator==(const ScriptValue&) const = default;

        /// <summary>データの深さ・総要素数・値を検証します。不正な値は例外で拒否します。</summary>
        void Validate(size_t depth, size_t& remaining) const;
        /// <summary>構造を複製せず、通常のScriptValueと同じ深さ・要素数・名前の規則で検証します。</summary>
        static void ValidateObject(const Object& object, size_t depth, size_t& remaining);

        /// <summary>配列・構造を再帰的に辿り、型付き個体参照だけを走査します。</summary>
        template <class Visitor> void VisitObjectReferences(Visitor&& visitor)
        {
            std::visit(
                [&](auto& item)
                {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, ScriptObjectReference>)
                    {
                        visitor(item.id);
                    }
                    else if constexpr (std::is_same_v<T, Array>)
                    {
                        for (auto& child : item)
                            child.VisitObjectReferences(visitor);
                    }
                    else if constexpr (std::is_same_v<T, Object>)
                    {
                        for (auto& [name, child] : item)
                        {
                            static_cast<void>(name);
                            child.VisitObjectReferences(visitor);
                        }
                    }
                },
                value);
        }
        /// <summary>複製された階層内のオブジェクト参照を新しいIDへ置き換えます。</summary>
        template <class IdMap> void Remap(const IdMap& ids)
        {
            VisitObjectReferences(
                [&](std::string& id)
                {
                    if (const auto found = ids.find(id); found != ids.end())
                        id = found->second;
                });
        }

    private:
        /// <summary>数値の有限性と保存範囲を検証します。</summary>
        static void ValidateItem(float item, size_t, size_t&)
        {
            if (!Engine::InRange(item, -1000000, 1000000))
                throw std::runtime_error("Invalid script data number");
        }
        /// <summary>真偽値は常に有効です。</summary>
        static void ValidateItem(bool, size_t, size_t&) {}
        /// <summary>文字列の保存範囲を検証します。</summary>
        static void ValidateItem(const std::string& item, size_t, size_t&)
        {
            if (item.size() > 4096 || item.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid script data string");
        }
        /// <summary>オブジェクト参照IDを検証します。</summary>
        static void ValidateItem(const ScriptObjectReference& item, size_t, size_t&)
        {
            if (item.id.size() > 128 || item.id.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid script object reference");
        }
        /// <summary>配列の要素数と子データを検証します。</summary>
        static void ValidateItem(const Array& item, size_t depth, size_t& remaining);
        /// <summary>構造のフィールド名と子データを検証します。</summary>
        static void ValidateItem(const Object& item, size_t depth, size_t& remaining);
    };
}
