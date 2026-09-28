#pragma once

namespace Engine
{
    /// <summary>
    /// クラッシュ処理とログを初期化し、エンジンの起動と終了を記録します。
    /// </summary>
    /// <returns>正常終了した場合は 0、初期化に失敗した場合は 1。</returns>
    int Run();
}
