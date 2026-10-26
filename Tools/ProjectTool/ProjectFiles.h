#pragma once
#include "ToolCommon.h"

namespace ProjectTool
{
    /// <summary>ゲームの宣言を検証し、エンジン参照を絶対パスへ解決します。</summary>
    Json LoadProject(const Path& project, const Path& engine = {});
    /// <summary>自動生成が許可されたゲームのビルドファイルを更新します。</summary>
    void SyncProject(const Path& project, const Path& engine = {});
    /// <summary>ひな形から独立した識別子を持つゲームを新規作成します。</summary>
    void NewProject(const Path& destination, const std::string& name, const Path& templateRoot, const Path& engine);
    /// <summary>素材の正本と出力先を分離して同期し、生成済みのDLLとシェーダーを保持します。</summary>
    void SyncContent(const Path& content, const Path& destination, const Path& engine = {});
    /// <summary>入力の変更を検出しながら、保存済み素材のスナップショットを作成します。</summary>
    void SnapshotContent(const Path& content, const Path& destination, bool includeEngineResources = true);
}
