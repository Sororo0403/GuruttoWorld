#include <SceneRuntime/Scripts/ScriptSchema.h>
#include <Engine/Core/FileContents.h>
#include <iostream>
#include <vector>

namespace
{
    std::string RawLiteral(const std::string& value)
    {
        for (size_t i = 0;; ++i)
        {
            const auto delimiter = "WP1_" + std::to_string(i);
            if (delimiter.size() > 16)
                throw std::runtime_error("Cannot encode schema literal");
            if (value.find(")" + delimiter + "\"") == std::string::npos)
                return "R\"" + delimiter + "(" + value + ")" + delimiter + "\"";
        }
    }
}

int wmain(int count, wchar_t** arguments)
{
    try
    {
        if (count != 2)
            throw std::runtime_error("Usage: ScriptSchemaCompiler <content root>");
        const auto root = std::filesystem::absolute(arguments[1]);
        const auto schemas = SceneRuntime::ScriptSchema::LoadProject(root);
        const auto sourceRoot = root / "Assets/Scripts";
        std::vector<std::pair<std::filesystem::path, std::string>> plans;
        if (!std::filesystem::is_directory(sourceRoot))
            return 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot))
        {
            const auto filename = Engine::FileContents::Text(entry.path().filename());
            if (!entry.is_regular_file() || !filename.ends_with(".schema.json"))
                continue;
            const auto document = Engine::Json::parse(*Engine::FileContents::Read(entry.path()));
            std::string text =
                "// スキーマのJSONから生成します。定義の変更はJSONへ記述してください。\n#pragma once\n#include <SceneRuntime/Scripts/ScriptRuntime.h>\n\nnamespace SceneRuntime::GeneratedSchemas\n{\n";
            for (const auto& [name, declaration] : document.at("scripts").items())
            {
                static_cast<void>(declaration);
                const Engine::Json metadata{{"version", 1}, {"scripts", {{name, schemas.at(name).Serialize()}}}};
                text += "    /// <summary>宣言の既定値と型を実行定義へ設定します。</summary>\n";
                text += "    inline SceneRuntime::ScriptDefinition " + name + "Schema()\n    {\n";
                text += "        static const auto schema = []\n        {\n            std::string source;\n";
                const auto json = metadata.dump();
                text += "            source.reserve(" + std::to_string(json.size()) + ");\n";
                for (size_t offset = 0; offset < json.size();)
                {
                    auto end = (std::min)(offset + 4096, json.size());
                    while (end < json.size() && (static_cast<unsigned char>(json[end]) & 0xc0) == 0x80)
                        --end;
                    text += "            source += " + RawLiteral(json.substr(offset, end - offset)) + ";\n";
                    offset = end;
                }
                text += "            return SceneRuntime::ScriptSchema::Parse(Engine::Json::parse(source)).at(\"" +
                        name + "\");\n        }();\n";
                text +=
                    "        SceneRuntime::ScriptDefinition result;\n        static_cast<SceneRuntime::ScriptSchema&>(result) = schema;\n        return result;\n    }\n";
            }
            text += "}\n";
            std::string windows;
            windows.reserve(text.size() + 128);
            for (const auto c : text)
            {
                if (c == '\n')
                    windows += '\r';
                windows += c;
            }
            auto path = entry.path();
            auto name = path.filename().native();
            name.resize(name.size() - std::wstring_view(L".schema.json").size());
            path.replace_filename(name + L".generated.h");
            plans.emplace_back(std::move(path), std::move(windows));
        }
        // 全宣言の検証と出力構築が済むまで、既存の生成物を変更しません。
        size_t changed = 0;
        for (const auto& [path, text] : plans)
            if (Engine::FileContents::Read(path) != text)
            {
                Engine::FileContents::Write(path, text);
                ++changed;
            }
        std::cout << "Schemas: " << schemas.size() << ", generated headers: " << plans.size()
                  << ", changed: " << changed << '\n';
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
