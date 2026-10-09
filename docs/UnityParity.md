# Unityに近い制作環境への拡張

2026-10-09開始。対象は機能不足の調査で挙げた全領域。検証済みの機能単位で、既存履歴と同様の日本語コミットを作成する。
宣言や保存形式だけの追加では完了にしない。EditorとAppの共通実行経路、編集・保存・Undo、失敗時の保持を確認する。

## 進捗

- [x] 型付きゲームデータ: 数値、真偽値、文字列、オブジェクト参照、配列、構造化データ、Inspector、複製・Prefab参照
- [ ] 更新タイミング: FixedUpdate、物理との同期、LateUpdate、Pause・Step、失敗時の状態保持
- [ ] スクリプト制作: 作成、編集、コンパイル、診断、再読み込み、公開フィールド、配布物への同梱
- [ ] 汎用UI: InputField、Slider、Toggle、ScrollView、Mask、自動レイアウト
- [ ] 複数シーン: 追加ロード、アンロード、シーン間の保持、Editor編集
- [ ] 音響: Listener、3D位置と減衰、Mixer、エフェクト、ストリーミング
- [ ] 描画規模: LOD、GPUインスタンシング、遮蔽カリング
- [ ] 照明: Point/Spotの影、IBL、ベイク照明、自動露出、カラーグレーディング
- [ ] ジャンル支援: NavMesh、Terrain、Tilemap、2D物理、Joint、ラグドール
- 対象外: 複数プラットフォーム対応。ユーザー指定によりWindows / DirectX 12を維持する。

Windows向けのEditorとAppで編集・実行・配布を確認する。

## 型付きゲームデータ

既存の `ScriptComponent.parameters` の数値設定は維持する。追加の `data` は `ScriptValue::Object`。
各値は数値、bool、UTF-8文字列、`ScriptObjectReference`、配列、名前付きフィールドの構造を保持する。
JSONは `kind` と `value` の組で型を明示し、普通の文字列とオブジェクト参照を混同しない。

`ScriptDefinition.dataFields` が公開フィールドの初期値と型を定義する。
`ScriptContext.Data` から保存された値、`Reference` から現在の参照先を取得する。
`dataState` は個体ごとの内部データで、フレームが失敗した場合に変更を取り消す。未設定・削除済みの参照先はnull。
Inspectorは型ごとの編集と階層からの参照ドラッグ、配列の追加・削除に対応する。
シーンの保存・Undo、Scriptによる複製、Editor複製、PrefabのID変換にも含まれる。

深さは8、全要素は4096、配列は256、オブジェクトは64フィールド、文字列は4096 bytes。
数値は有限かつ絶対値1000000以内。不正なデータは保存・実行時に拒否する。
未知のゲーム処理の保存値は保持し、実行時には未登録として診断する。

組み込み `FollowTarget` は `active`、`target`、`offset.x/y/z` を使って同じ親の対象を追従する。
親が異なる対象は座標系の混同を防ぐためエラーとする。異なる親の追従は今後のWorld変換APIで扱う。

検証: Debug・Development・Releaseのソリューションビルド、Debug全体回帰、Debug／ReleaseのScriptデータとGPU実行検証が成功。
GPU実行検証にはEditor複製の参照変換、Undoによる保存データ・参照の復元、組み込みFollowTargetの動作を含む。
Release回帰テストのUI検証に必要なImGuiのリンク漏れをテストプロジェクト側で修正した。
追加したScriptValueとScriptDataPanelは複雑度基準内。リポジトリ全体の複雑度チェックには既存コードの超過が残っており、全体の静的解析を合格扱いにしない。
