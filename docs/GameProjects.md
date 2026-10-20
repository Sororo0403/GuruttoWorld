# エディターとゲームプロジェクト

`Editor.slnx` はDebug／Developmentでエディター・Engine・SceneRuntimeをビルドします。ゲーム本体とゲーム処理DLLはビルドしません。
`Game.slnx` は `Games/GuruttoWorld/GuruttoWorld.slnx` と同じ移行済みゲームをビルドします。`WP1.slnx` は全体開発用です。
既存のシーン・Prefab・素材・ゲーム処理・設定は `Games/GuruttoWorld` に移行済みで、エディターとゲームの作業を分ける運用は[並行開発](#並行開発)に記載しています。

## 独立したゲームを作成する

Visual StudioのC++環境が必要です。リポジトリのルートから実行します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/NewGameProject.ps1 -Destination C:\Games\MyGame -Name MyGame
```

新しいフォルダーへ `Content/Templates/MinimalGame` の空のシーン・起動設定を複製します。タイトル・メニュー・ゲーム処理は含みません。既存フォルダーは上書きしません。
ぐるっとワールドをひな型にする場合は `-TemplateContent Games/GuruttoWorld` を明示します。
別のゲームをひな型にする場合は `-TemplateContent C:\Games\ExistingGame` を指定します。
ゲーム名は英字で始まる英数字・アンダースコア、64文字以内です。

```text
MyGame/
  WP1.project.json         エンジンの参照先とソリューション情報
  MyGame.slnx              ゲーム専用ソリューション
  MyGame.vcxproj           共通プレイヤーを参照するビルド設定
  Assets/                 シーン・Prefab・素材・C++ゲーム処理
    Project.json          起動シーン・ウィンドウ・入力設定
    Scripts/
  Shaders/
  generated/              実行ファイル・中間ファイル・ログ・配布物
```

ゲームフォルダーは別のGitリポジトリで管理できます。ゲーム固有の処理は `Assets/Scripts` に置きます。
プレイヤーの共通コードをゲームごとに複製せず、WP1のAppプロジェクトを参照します。
Engine・SceneRuntimeのソースと依存ライブラリは指定したWP1のチェックアウトを使用します。
この方式はエンジンのソース参照方式です。エンジンのソースなしで使えるバイナリSDKではありません。

## 開く・ビルドする

エディターの「ファイル → ゲームプロジェクトを開く（別ウィンドウ）」で `WP1.project.json` を選びます。
現在のウィンドウと未保存の編集を保持したまま、選択したゲームを別のウィンドウで開きます。
コマンドラインからも開けます。

```powershell
& .\generated\outputs\x64\Development\Editor\Editor.exe --project C:\Games\MyGame
```

初期シーンはゲーム側の `Assets/Project.json` にある起動シーンです。WP1内で引数なしで起動すると、移行済みの `Games/GuruttoWorld` を開きます。
`--scene Assets/Scenes/Other.json` で指定できます。エディターUIのフォントはエディター側のContentを使用します。
`--validate-project --project <ゲームフォルダー>` はウィンドウを開かず、プロジェクト情報・起動設定・起動シーンを検証し、失敗時に終了コード1を返します。

ゲーム側の `MyGame.slnx` をVisual Studioで開き、Debug／Development／Release、x64でビルドします。
実行ファイルは `generated/outputs/x64/<構成>/App/MyGame.exe` です。
各構成のゲーム処理DLLとゲーム側のAssets・Shadersを同じ出力先に配置します。
ゲーム処理のソースはソリューションにも表示し、DLLのコンパイルは共通のビルド処理が担当します。

エディターのビルド画面は開いているゲームの専用プロジェクトをビルド・パッケージ化します。
コマンドラインで配布物を作る場合は次のように指定します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/BuildPlayer.ps1 -Project C:\Games\MyGame -Configuration Release
```

配布物・進捗・ログはゲーム側の `generated/builds`、ゲーム処理のログは `generated/script-builds` に出力します。
元のゲーム素材をスナップショット化して配布物を作るため、ビルド中の変更を検出した場合は失敗として報告します。

## 並行開発

ゲーム制作のチャットはゲームフォルダー、エディター修正のチャットはWP1を作業場所にします。
共通Engine・SceneRuntimeを変更するときは、検証した変更をゲーム側へ取り込んでください。
同じWP1チェックアウトを参照するゲームは共通ライブラリのビルド出力を共有します。
共通部分も同時にビルド・変更する場合は、ゲームが参照するWP1を別worktreeにして競合を避けます。
新規プロジェクトの参照先は `WP1.project.json` の `engineRoot` が正本です。移動後は `scripts/SyncGameProject.ps1 -Project <ゲームフォルダー>` で `.vcxproj` と `.slnx` を再生成します。配布ビルドも生成対象のファイルを同期してから実行します。
`generatedProjects` がない旧プロジェクトは手書きのビルド設定を保持し、自動再生成しません。その場合は従来どおり descriptor・プロジェクト・ソリューションの参照を揃えます。
Editor は自身と異なるエンジンチェックアウトのプロジェクトを開く前に拒否します。指定したチェックアウトで構築した Editor を使用してください。
新規プロジェクトの `projectId` はユーザー設定の保存先にも使い、移動や Project.json のメタデータ変更から独立させます。旧プロジェクトは従来のアセット ID を引き継ぎます。
移行済みの `Games/GuruttoWorld` は相対パス `../..` を使用するため、WP1の別worktreeではそのworktreeのEngine・SceneRuntimeを参照します。

## 再生処理の共有

エディターのGame再生と配布プレイヤーは、`SceneRuntime::GamePlayback` と `TitleController` を共用します。
AppとEditorでゲームの動作を別々に実装せず、共通処理またはゲーム側のScriptへ追加します。
Editorは編集データの複製・Pause・StepとGameパネルの座標変換を担当し、Appはウィンドウ入力を取得します。QUITはEditorでは再生停止、Appでは終了として受け取ります。
設定の保存先は両方とも `GameSettings::UserPath()` です。CONFIGの移動先・戻り先・時間はCanvasのメニュー設定とシーンのTransformで編集します。
共通の再生動作は `GamePlaybackParityValidation`、CONFIGの停止・再開・復元は `TitleConfigTransitionValidation` で検証します。

## スクリプト宣言と任意のゲーム機能

新規スクリプトは処理用CPPと同名の`*.schema.json`を持ちます。設定の既定値・範囲・型・素材用途はJSONに記述し、CPPは生成される`*.generated.h`の`SceneRuntime::GeneratedSchemas::<動作名>Schema()`にコールバックを付けます。生成物を直接編集しないでください。InspectorはDLLを読み込まず宣言を表示し、DLLのロード時には宣言との一致を検証します。宣言を変更したらスクリプトを再ビルドし、既存シーンへの既定値追加はInspectorの明示的な移行操作で行います。

version 1の宣言は従来どおり使用できます。version 2では動作に`"extends": "基底の動作名"`を指定でき、同じプロジェクト内の別ファイルも参照できます。parameters／data／purposesをキー単位で上書きし、不要な継承項目はnullで削除します。配列や構造の既定値は項目全体の置換です。循環・基底欠損・16段を超える継承・用途と型の不一致はビルド前に拒否します。派生物の生成とEditorの読込は同じScriptSchemaの解析を使います。

```powershell
./scripts/GenerateScriptSchemas.ps1 -Content 'C:/Games/MyGame' -Configuration Debug
./scripts/BuildScripts.ps1 -Content 'C:/Games/MyGame' -Configuration Debug
```

共通プレイヤーはProject設定のstartupSceneを通常のAuthoredSceneとして起動します。Titleという名前から起動先を推測しません。メニュー、空、粒子、海、UIなどはシーンの有効なコンポーネントに応じて準備し、空のゲームにそれらのシェーダーを要求しません。Editorは起動引数のプロジェクト、現在フォルダーのdescriptor、または選択ダイアログから編集対象を決めます。

ビルドはゲーム固有のvcxproj／slnxを入口にしてください。App/App.vcxprojを直接使う場合はGameContentRootを明示します。Editorと検証用の共通コンテンツの既定値はContentであり、特定ゲームを自動的に含めません。

既存の外部ゲームを移行する場合は、各動作の宣言を用意し、生成関数の呼出しを`SceneRuntime::GeneratedSchemas`へ更新してから、対応する構成のSDKとDLLを再ビルドします。シーンに保存済みの値の補完は明示的な移行として確認します。手順と互換経路は[根本設計の段階移行](ArchitectureMigration.md)を参照してください。
