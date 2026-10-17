# ぐるっとワールド

既存のゲームを移行した独立プロジェクトです。起動シーンは `Assets/Project.json` の指定を維持し、現在は `Assets/Scenes/OceanSunset.json` です。

`GuruttoWorld.slnx` をVisual Studioで開き、Debug／Development／Release、x64でビルドします。
実行ファイルは `generated/outputs/x64/<構成>/App/GuruttoWorld.exe` です。

エディターはWP1の `Editor.slnx` からビルドし、次のようにこのプロジェクトを開きます。

```powershell
& ..\..\generated\outputs\x64\Development\Editor\Editor.exe --project .
```

シーン・Prefab・素材・C++ゲーム処理は、このプロジェクトの `Assets` で制作します。シェーダーは `Shaders`、ゲームの起動・入力設定は `Assets/Project.json` に保存します。
Engine・SceneRuntime・共通プレイヤーは `../..` のWP1を参照します。プロジェクトをWP1ごと別worktreeへ移しても、参照先はそのworktree内に切り替わります。

エディター修正と並行して制作する運用は[並行開発の説明](../../docs/GameProjects.md#並行開発)を参照してください。
