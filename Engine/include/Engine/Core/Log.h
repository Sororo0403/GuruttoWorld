#pragma once

#include <filesystem>
#include <string_view>
#include <string>
#include <vector>
#include <cstdint>

namespace Engine
{
    enum class LogLevel
    {
        Debug,
        Info,
        Warning,
        Error
    };

    struct LogEntry
    {
        std::uint64_t sequence = 0;
        LogLevel level = LogLevel::Info;
        std::string text;
    };

    class Log final
    {
    public:
        /// <summary>
        /// 静的関数のみを使用するため、インスタンスの生成を禁止します。
        /// </summary>
        Log() = delete;

        /// <summary>
        /// ログファイルを追記モードで開きます。失敗してもデバッグ出力は利用できます。
        /// </summary>
        /// <param name="filePath">出力先のパス。空の場合は LocalAppData/WP1/logs/App.log を使用します。</param>
        /// <returns>ログファイルを開けた場合は true、失敗した場合は false。</returns>
        static bool Initialize(const std::filesystem::path& filePath = {});

        /// <summary>
        /// ログファイルを閉じます。デバッグ出力は引き続き利用できます。
        /// </summary>
        static void Shutdown();

        // Thread-safe snapshots of the latest 500 entries. Clear only affects this in-memory history.
        static std::vector<LogEntry> Recent();
        static void ClearRecent();

        /// <summary>
        /// 日時とログレベルを付けてデバッグ出力とログファイルに書き込み、ファイルを即時フラッシュします。
        /// </summary>
        /// <param name="level">出力するログのレベル。</param>
        /// <param name="message">UTF-8 形式のメッセージ。</param>
        static void Write(LogLevel level, std::string_view message);

        /// <summary>
        /// デバッグレベルのログを出力します。
        /// </summary>
        /// <param name="message">UTF-8 形式のメッセージ。</param>
        static void Debug(std::string_view message);

        /// <summary>
        /// 情報レベルのログを出力します。
        /// </summary>
        /// <param name="message">UTF-8 形式のメッセージ。</param>
        static void Info(std::string_view message);

        /// <summary>
        /// 警告レベルのログを出力します。
        /// </summary>
        /// <param name="message">UTF-8 形式のメッセージ。</param>
        static void Warning(std::string_view message);

        /// <summary>
        /// エラーレベルのログを出力します。
        /// </summary>
        /// <param name="message">UTF-8 形式のメッセージ。</param>
        static void Error(std::string_view message);
    };
}
