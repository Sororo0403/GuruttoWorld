# 街の配置エディター

`WP1.slnx` の Debug または Development をビルドし、
`generated/outputs/x64/<構成>/Editor/Editor.exe` を起動します。
Release のソリューションビルドはゲームを対象とし、Editor は除外します。

- 右ドラッグ：視点回転
- 右ドラッグ＋WASD：移動、Q/E：上下移動、Shift：加速
- R／Reset camera：初期視点へ戻る

現在は街の読み込みと自由カメラまで実装しています。配置編集・保存は次の段階です。

## 依存関係

```text
App ──────→ SceneRuntime ──→ Engine
Editor ───→ SceneRuntime ──→ Engine
```

SceneRuntime は配置データとモデルの描画を担当し、ゲーム入力や編集UIを含みません。
App はタイトルのUI・空・光の粒・待機演出を担当します。
Editor は独自の起動処理・自由カメラ・編集UIを担当し、Appを参照しません。

モデル・JSON・共有のメッシュシェーダーは `Content` にあります。
Appへの配置は `Content/Content.targets` が行います。
開発環境のEditorは実行ファイルから上位へ探索して、元の `Content` を読みます。
元のContentが見つからない配布環境では、実行ファイルに同梱されたデータを読みます。
このため開発時に扱うJSONは、ビルド出力にあるコピーではなく
`Content/Assets/Scenes/TitleStreet.json` です。
