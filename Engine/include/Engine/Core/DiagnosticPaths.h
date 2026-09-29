#pragma once

#include <filesystem>

namespace Engine
{
    /// <summary>
    /// ユーザーの LocalAppData 配下の WP1 フォルダーを取得します。取得失敗時は空を返します。
    /// フォルダーやファイルは作成しません。
    /// </summary>
    std::filesystem::path GetDiagnosticsRoot();
}
