# プロジェクト管理ツール

エディターのゲーム処理コンパイル・配布ビルドと、MSBuildの素材同期に使うC++製CLIです。
ゲーム作成・プロジェクト再生成も同じ実装を使用し、エディターと素材同期はこのCLIを直接呼び出します。
依存ライブラリ構築やエンジン識別子生成など、開発時のMSBuild処理には従来のPS1を使います。

`WP1.slnx` または `ProjectTool.vcxproj` をx64でビルドすると、
`generated/outputs/x64/<構成>/ProjectTool/ProjectTool.exe` が生成されます。
Editorのビルドでは、同じ構成のツールをEditor.exeの隣へコピーします。
ゲームのビルドでは `Engine/build/ProjectTool.targets` がツールを用意します。

```powershell
$tool = '.\generated\outputs\x64\Development\ProjectTool\ProjectTool.exe'
& $tool new-project --destination C:\Games\MyGame --name MyGame
& $tool sync-project --project C:\Games\MyGame
& $tool build-scripts --content C:\Games\MyGame --configuration Development
& $tool build-player --project C:\Games\MyGame --configuration Release
& $tool --help
```

ツールをエンジンのチェックアウト外にコピーした場合は `--engine <エンジンのルート>` を指定します。
実際のコンパイルにはVisual StudioのC++環境とMSBuildが必要です。

`ToolCommon` はプロセス・引数・SHA-256・原子的なJSON保存、`ProjectFiles` はプロジェクトと素材、
`ScriptBuild` はゲーム処理DLL、`PackageBuild` は配布物作成を扱います。
DLLの公開先とSDKビルドを名前付きミューテックスで排他し、入力の変更やコンパイル失敗では以前の公開DLLを保持します。
配布物はスナップショットから構築し、シェーダー準備と起動検証に成功してから完成フォルダーへ移します。

回帰試験は `tests/ProjectToolValidation.vcxproj`、`tests/TestScriptPublication.ps1` と、
`TestProjectContext`・`TestGameProjects`・`TestContentSynchronization`・`TestBuildContentSnapshot`・
`TestEngineResources`・`TestScriptSchemaLoading` で実行します。

`scripts` に残るものは、依存ライブラリ構築・エンジン識別子生成・整形・静的解析・旧シーン変換とその補助処理です。
