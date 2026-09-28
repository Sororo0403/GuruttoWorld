#pragma once

namespace Engine
{
    /// <summary>
    /// クラッシュ処理とログを初期化し、ウィンドウが閉じられるまでメッセージを処理します。
    /// </summary>
    /// <returns>ウィンドウの終了コード。初期化やメッセージ処理に失敗した場合は 1。</returns>
    int Run();
}
