#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ProjectTool
{
    using Path = std::filesystem::path;
    using Json = nlohmann::json;

    struct Options final
    {
        Path engine, content, output, work, project;
        std::string configuration = "Development", runId, startupScene;
        bool skipBuild = false;
    };

    /// <summary>パスをUTF-8へ変換します。</summary>
    std::string Utf8(const Path& path);
    /// <summary>UTF-8をWindowsの文字列へ変換します。</summary>
    std::wstring Wide(std::string_view text);
    /// <summary>UTF-8からパスを作成します。</summary>
    Path FromUtf8(std::string_view text);
    /// <summary>JSONファイルを読み込みます。</summary>
    Json ReadJson(const Path& path);
    /// <summary>親ディレクトリーを作成してUTF-8を書き込みます。</summary>
    void WriteText(const Path& path, std::string_view text);
    /// <summary>JSONを一時ファイルから原子的に公開します。</summary>
    void WriteJsonAtomic(const Path& path, const Json& value);
    /// <summary>ファイルのSHA-256を小文字の16進数で返します。</summary>
    std::string Sha256(const Path& path);
    /// <summary>文字列のSHA-256を小文字の16進数で返します。</summary>
    std::string Sha256Text(std::string_view text);
    /// <summary>衝突しない作業識別子を作成します。</summary>
    std::string NewId();
    /// <summary>XMLの属性値とテキストをエスケープします。</summary>
    std::string XmlEscape(std::string_view text);
    /// <summary>Visual StudioのMSBuildを検索します。</summary>
    Path FindMsbuild();
    /// <summary>実行中のツールの絶対パスを取得します。</summary>
    Path CurrentExecutable();
    /// <summary>Windowsのコマンドライン引数を引用します。</summary>
    std::wstring QuoteArgument(std::wstring_view argument);
    /// <summary>子プロセスを非表示で実行し、出力をログへ追記します。</summary>
    int RunProcess(const Path& executable, const std::vector<std::wstring>& arguments, const Path& cwd, const Path& log,
                   unsigned timeoutMs = INFINITE);
    /// <summary>エディターから読み取る進捗を公開します。</summary>
    void Progress(const Options& options, bool scripts, int step, std::string_view phase);
}
