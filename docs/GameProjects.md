# エディターとゲームプロジェクト

`Editor.slnx` はDebug／Developmentでエディター・Engine・SceneRuntimeをビルドします。ゲーム本体とゲーム処理DLLはビルドしません。
`Game.slnx` は `Games/GuruttoWorld/GuruttoWorld.slnx` と同じ移行済みゲームをビルドします。`WP1.slnx` は全体開発用です。
既存のシーン・Prefab・素材・ゲーム処理・設定は `Games/GuruttoWorld` に移行済みで、エディターとゲームのチャットを分ける運用は[並行開発の説明](ParallelDevelopment.md)に記載しています。

## 独立したゲームを作成する

Visual StudioのC++環境が必要です。リポジトリのルートから実行します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/NewGameProject.ps1 -Destination C:\Games\MyGame -Name MyGame
```

新しいフォルダーへ現在の `Games/GuruttoWorld` のAssets・Shadersを複製します。既存フォルダーは上書きしません。
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
外部プロジェクトの参照先を移すときは `WP1.project.json` の `engineRoot`、`.vcxproj` の `WP1Root`、`.slnx` のEngine・SceneRuntime参照を同じチェックアウトへ変更します。
移行済みの `Games/GuruttoWorld` は相対パス `../..` を使用するため、WP1の別worktreeではそのworktreeのEngine・SceneRuntimeを参照します。
