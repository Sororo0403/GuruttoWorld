#pragma once
// nlohmann/json は 3.12.0 に固定します。Engine/externals/nlohmann/README.md を参照してください。
#include "../../../externals/nlohmann/json.hpp"
#include <stdexcept>
namespace Engine
{
    using Json = nlohmann::json;
    /// <summary>数値型を検証して値を取得します。</summary>
    double JsonNumber(const Json& value);
    /// <summary>配列型を検証して参照を取得します。</summary>
    const Json& JsonArray(const Json& value);
    /// <summary>オブジェクト型を検証して参照を取得します。</summary>
    const Json& JsonObject(const Json& value);
}
