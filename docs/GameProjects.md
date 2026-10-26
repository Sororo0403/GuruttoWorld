# エディターとゲームプロジェクト

エンジン・エディター開発のソリューションは WP1 ルートの `WP1.slnx` に統一しています。Debug／Development、x64 で Editor・ProjectTool・Player・Engine・SceneRuntime・SceneData・SceneStorage をビルドします。
Release は ProjectTool と Player・Engine・SceneRuntime・SceneData・SceneStorage のライブラリをビルドし、Editor は除外します。いずれの構成もゲーム本体とゲーム処理DLLはビルドしません。エンジン内の `Game.slnx` は廃止しました。
ぐるっとワールドはエンジン外の `../GuruttoWorld` にある独立リポジトリです。シーン・Prefab・素材・ゲーム処理・制作スクリプト・制作テストはゲーム側で管理します。
ゲーム側の `Build.ps1`・`Run.ps1`・`OpenEditor.ps1` を入口にします。
旧データは移行時のローカルバックアップ `generated/game-separation/original` に退避しています。Git 管理対象のゲームソースはエンジン内に残しません。

```powershell
# ぐるっとワールド側で実行します。
./Build.ps1 -Configuration Development
./Run.ps1 -Configuration Development -SkipBuild
./OpenEditor.ps1
```

## 独立したゲームを作成する

Visual StudioのC++環境が必要です。先に `WP1.slnx` のDevelopment・x64をビルドし、リポジトリのルートから実行します。以下の `$tool` は、各コマンドを実行するPowerShellで設定してください。

```powershell
$tool = '.\generated\outputs\x64\Development\ProjectTool\ProjectTool.exe'
& $tool new-project --destination C:\Games\MyGame --name MyGame
```

新しいフォルダーへ `Tools/GameProject/Templates/MinimalGame` の空のシーン・起動設定を複製します。タイトル・メニュー・ゲーム処理は含みません。既存フォルダーは上書きしません。
ぐるっとワールドをひな型にする場合は `--template-content ../GuruttoWorld` を明示します。
別のゲームをひな型にする場合は `--template-content C:\Games\ExistingGame` を指定します。
ゲーム名は英字で始まる英数字・アンダースコア、64文字以内です。
標準シェーダーやEditorのフォントは新規ゲームへ複製しません。標準描画はエンジンが所有する `Engine/Shaders` を使用し、ゲームの独自シェーダーは `Shaders` に置きます。既存マテリアルの標準シェーダーへの旧パス・ID参照は読み込み時に維持します。

```text
MyGame/
  WP1.project.json         エンジンの参照先とソリューション情報
  MyGame.slnx              ゲーム専用ソリューション
  MyGame.vcxproj           Playerライブラリを参照するビルド設定
  src/main.cpp            ゲームが所有する起動処理
  Assets/                 シーン・Prefab・素材・C++ゲーム処理
    Project.json          起動シーン・ウィンドウ・入力設定
    Scripts/
  Shaders/                ゲーム独自シェーダー（必要な場合だけ作成）
  generated/              実行ファイル・中間ファイル・ログ・配布物
```

ゲームフォルダーは別のGitリポジトリで管理できます。ゲーム固有の処理は `Assets/Scripts` に置きます。
プレイヤーの共通コードをゲームごとに複製せず、ゲーム側に `src/main.cpp` を置き、WP1の共通起動ライブラリ `Player` を参照します。
Engine・SceneRuntimeのソースと依存ライブラリは指定したWP1のチェックアウトを使用します。
この方式はエンジンのソース参照方式です。エンジンのソースなしで使えるバイナリSDKではありません。

## 開く・ビルドする

エディターの「ファイル → ゲームプロジェクトを開く（別ウィンドウ）」で `WP1.project.json` を選びます。
現在のウィンドウと未保存の編集を保持したまま、選択したゲームを別のウィンドウで開きます。
コマンドラインからも開けます。

```powershell
& .\generated\outputs\x64\Development\Editor\Editor.exe --project C:\Games\MyGame
```

初期シーンはゲーム側の `Assets/Project.json` にある起動シーンです。引数なしでは現在のフォルダーの descriptor、または選択ダイアログからプロジェクトを決めます。
`--scene Assets/Scenes/Other.json` で指定できます。エディターUIのフォントは `Editor/Resources/Fonts` を使用します。
`--validate-project --project <ゲームフォルダー>` はウィンドウを開かず、プロジェクト情報・起動設定・起動シーンを検証し、失敗時に終了コード1を返します。

ゲーム側の `MyGame.slnx` をVisual Studioで開き、Debug／Development／Release、x64でビルドします。
実行ファイルは `generated/outputs/x64/<構成>/App/MyGame.exe` です。
各構成のゲーム処理DLL、ゲーム側の `Assets`・`Shaders`、標準描画用の `Engine/Shaders` を同じ出力先に配置します。Editor専用フォントはゲームへコピーしません。
ゲーム処理のソースはソリューションにも表示し、DLLのコンパイルは共通のビルド処理が担当します。

エディターのビルド画面は開いているゲームの専用プロジェクトをビルド・パッケージ化します。
コマンドラインで配布物を作る場合は次のように指定します。

```powershell
& $tool build-player --project C:\Games\MyGame --configuration Release
```

配布物・進捗・ログはゲーム側の `generated/builds`、ゲーム処理のログは `generated/script-builds` に出力します。
元のゲーム素材をスナップショット化して配布物を作るため、ビルド中の変更を検出した場合は失敗として報告します。
エディターも同じC++製 `ProjectTool.exe` を非表示の子プロセスとして呼びます。ゲーム処理のコンパイル、素材同期、パッケージ作成の本体はC++で実装し、PowerShellスクリプトへ委譲しません。エンジンのソースビルドに使う識別子生成などの開発用PS1は残します。子プロセスを所有するジョブにより、キャンセルやエディター終了時はMSBuildも停止します。

## 並行開発

ゲーム制作のチャットはゲームフォルダー、エディター修正のチャットはWP1を作業場所にします。
共通Engine・SceneRuntimeを変更するときは、検証した変更をゲーム側へ取り込んでください。
同じWP1チェックアウトを参照するゲームは共通ライブラリのビルド出力を共有します。
共通部分も同時にビルド・変更する場合は、ゲームが参照するWP1を別worktreeにして競合を避けます。
新規プロジェクトの参照先は `WP1.project.json` の `engineRoot` が正本です。移動後は `ProjectTool.exe sync-project --engine <参照先のWP1> --project <ゲームフォルダー>` で `.vcxproj` と `.slnx` を再生成します。配布ビルドも生成対象のファイルを同期してから実行します。
`generatedProjects` がない旧プロジェクトは手書きのビルド設定を保持し、自動再生成しません。その場合は従来どおり descriptor・プロジェクト・ソリューションの参照を揃えます。
Editor は自身と異なるエンジンチェックアウトのプロジェクトを開く前に拒否します。指定したチェックアウトで構築した Editor を使用してください。
新規プロジェクトの `projectId` はユーザー設定の保存先にも使い、移動や Project.json のメタデータ変更から独立させます。旧プロジェクトは従来のアセット ID を引き継ぎます。
ぐるっとワールドは descriptor の `engineRoot` に `../WP1` を指定し、`generatedProjects: true` でビルドファイルを同期します。別のエンジンチェックアウトを使う場合は参照先を明示的に変更してください。ゲームIDと素材IDは移動前の値を維持します。

## 再生処理の共有

エディターのGame再生と配布プレイヤーは `SceneRuntime::GamePlayback` を共用します。Titleは任意の `PlaybackFeature` として注入し、`SceneTitle` ターゲットで実装します。Titleを使うゲームはdescriptorに `"titleFeature": true` を指定し、`Player::Options::playbackFeature` に `TitleController` のfactoryを渡します。既定の最小ゲームは共通再生だけをリンクします。
プレイヤーとEditorでゲームの動作を別々に実装せず、共通処理またはゲーム側のScriptへ追加します。
Editorは編集データの複製・Pause・StepとGameパネルの座標変換を担当し、プレイヤーはウィンドウ入力を取得します。QUITはEditorでは再生停止、プレイヤーでは終了として受け取ります。
設定の保存先は両方とも `GameSettings::UserPath()` です。CONFIGの移動先・戻り先・時間はCanvasのメニュー設定とシーンのTransformで編集します。
共通の再生動作は `GamePlaybackParityValidation`、CONFIGの停止・再開・復元は `TitleConfigTransitionValidation` で検証します。

## スクリプト宣言と任意のゲーム機能

新規スクリプトは処理用CPPと同名の`*.schema.json`を持ちます。設定の既定値・範囲・型・素材用途はJSONに記述し、CPPは`SceneRuntime::RuntimeScriptDefinition`へ `RuntimeScriptContext&` を受け取る処理を設定し、動作名とともに登録します。EXE側がJSONを直接読み、同名の処理へScriptSchemaを結合します。Inspectorも同じJSON解析を使い、DLLなしで宣言を表示します。生成ヘッダーは使用しません。JSONだけの変更は再生停止中に再読み込みし、DLLの再ビルドは不要です。不正な宣言では直前の有効な登録を保持します。既存シーンへの既定値追加はInspectorの明示的な移行操作で行います。

version 1の宣言は従来どおり使用できます。version 2では動作に`"extends": "基底の動作名"`を指定でき、同じプロジェクト内の別ファイルも参照できます。parameters／data／purposesをキー単位で上書きし、不要な継承項目はnullで削除します。配列や構造の既定値は項目全体の置換です。循環・基底欠損・16段を超える継承・用途と型の不一致はJSONの読み込み時に拒否します。Editorとプレイヤーは同じScriptSchemaの解析を使います。

```powershell
& $tool build-scripts --engine . --content 'C:/Games/MyGame' --configuration Debug
```

共通プレイヤーはProject設定のstartupSceneを通常のAuthoredSceneとして起動します。Titleという名前から起動先を推測しません。メニュー、空、粒子、海、UIなどはシーンの有効なコンポーネントに応じて準備し、空のゲームにそれらのシェーダーを要求しません。Editorは起動引数のプロジェクト、現在フォルダーのdescriptor、または選択ダイアログから編集対象を決めます。

ビルドはゲーム固有のvcxproj／slnxを入口にしてください。ゲーム側の `src/main.cpp` をコンパイルし、`Engine/build/GameProject.targets` の設定で `Player/Player.vcxproj` を参照します。Editorのフォントは `Editor/Resources/Fonts`、標準シェーダーは `Engine/Shaders` に分離しています。ゲーム素材は明示したプロジェクトだけを使用します。

既存の外部ゲームを移行する場合は、各動作の宣言を用意し、生成ヘッダーのincludeと生成関数の呼び出しを削除します。CPPは `RuntimeScriptDefinition` を登録する形へ変更し、`context.object` への直接変更をReadViewと型付きsetterに置き換えて、対応する構成のSDKとDLLをABI 18で再ビルドします。シーンに保存済みの値の補完は明示的な移行として確認します。手順と互換経路は[根本設計の段階移行](ArchitectureMigration.md)を参照してください。

外部ゲームの C++ 整形は、エンジン側から `scripts/FormatCode.ps1 -RepositoryRoot ../GuruttoWorld -Paths Assets/Scripts/SunriseStreet.cpp` のように対象リポジトリを指定できます。
