#pragma once

#include <filesystem>

namespace Engine
{
    class CrashHandler final
    {
    public:
        /// <summary>
        /// 静的関数のみを使用するため、インスタンスの生成を禁止します。
        /// </summary>
        CrashHandler() = delete;

        /// <summary>
        /// 未処理例外と std::terminate の記録処理を登録します。起動時にメインスレッドから呼び出してください。
        /// 登録済みの場合は出力先を変更せずに成功を返します。
        /// </summary>
        /// <param name="directory">保存先。空の場合は LocalAppData/WP1/crashes フォルダーを使用します。</param>
        /// <returns>登録に成功した場合は true、保存先や記録用スレッドの準備に失敗した場合は false。</returns>
        static bool Initialize(const std::filesystem::path& directory = {});

        /// <summary>
        /// 登録前のハンドラーを復元し、記録用スレッドを終了します。
        /// 他の処理が終了した後にメインスレッドから呼び出してください。
        /// </summary>
        static void Shutdown();
    };
}
