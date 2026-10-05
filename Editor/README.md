# 街の配置エディター

`WP1.slnx` の Debug または Development をビルドし、
`generated/outputs/x64/<構成>/Editor/Editor.exe` を起動します。
Release のソリューションビルドはゲームを対象とし、Editor は除外します。

- 右ドラッグ：視点回転
- 右ドラッグ＋WASD：移動、Q/E：上下移動、Shift：加速
- R／Reset camera：初期視点へ戻る

Objectsで名前・ID・モデル名を検索して対象を選び、Inspectorで位置・回転・大きさを編集できます。
数値はドラッグ、またはCtrl＋クリックで直接入力できます。回転の表示単位は度、JSONはラジアンです。
変更は即座に描画へ反映されます。SaveでJSONへ保存、Reloadでディスクから再読み込みします。
未保存でReloadを押すと、保存して読み込み・破棄して読み込み・キャンセルを選べます。
読み込みや保存が失敗した場合は、今の配置と未保存状態を維持してエラーを表示します。
Editorを閉じる前にはSaveを押してください。ゲームには次のビルドで保存したJSONがコピーされます。

Modelsでモデル名やカテゴリを検索し、Add selected modelで追加できます。
追加位置は数値入力、またはUse camera frontでカメラの約8単位先の地面へ合わせます。
新しいモデルの拡縮は4です。追加後は自動選択され、Inspectorで調整できます。
InspectorのDuplicateはX方向へ4単位ずらして複製し、Deleteは選択対象を削除します。
追加・複製・削除の結果もSaveでJSONに保存されます。Undoは後続の段階で追加します。

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
