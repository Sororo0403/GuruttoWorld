# Unityに近い制作環境への拡張

2026-10-09開始。対象は機能不足の調査で挙げた全領域。検証済みの機能単位で、既存履歴と同様の日本語コミットを作成する。
宣言や保存形式だけの追加では完了にしない。EditorとAppの共通実行経路、編集・保存・Undo、失敗時の保持を確認する。

## 進捗

- [x] 型付きゲームデータ: 数値、真偽値、文字列、オブジェクト参照、配列、構造化データ、Inspector、複製・Prefab参照
- [x] 更新タイミング: FixedUpdate、物理との同期、LateUpdate、Pause・Step、失敗時の状態保持
- [x] スクリプト制作: 作成、編集、コンパイル、診断、再読み込み、公開フィールド、配布物への同梱
- [x] 汎用UI: InputField、Slider、Toggle、ScrollView、Mask、自動レイアウト
- [x] 複数シーン: 追加ロード、アンロード、シーン間の保持、Editor編集
- [x] 音響: Listener、3D位置と減衰、Mixer、エフェクト、ストリーミング
- [x] 描画規模: LOD、GPUインスタンシング、遮蔽カリング
- [x] 照明: Point/Spotの影、IBL、ベイク照明、自動露出、カラーグレーディング
- [x] ジャンル支援: NavMesh、Terrain、Tilemap、2D物理、Joint、ラグドール
- 対象外: 複数プラットフォーム対応。ユーザー指定によりWindows / DirectX 12を維持する。

Windows向けのEditorとAppで編集・実行・配布を確認する。チェックは実装の完了を表す。

検証状況: 全変更を統合したDebug・Development・Releaseのソリューションビルド、各領域のDebug個別回帰、Debug／Releaseの音声デコード・ストリーミング、実DLLの登録と再読み込み、自動変更監視、コンパイル失敗時の保持、Contentスナップショット、Release配布物の起動検証が成功。Debug／Releaseの全体回帰と全追加領域の個別回帰、配布Appでの汎用UI・ラグドール両デモの読み込み・GPU描画も成功。下記の先行機能に記載した成功結果は、その機能を追加した時点の検証結果。

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

## 更新段階

`ScriptDefinition.fixedUpdate` と `lateUpdate` を追加し、通常のupdateがない処理も登録できる。
Contextのphaseで呼び出された段階を判別する。startは段階をまたいで一回だけ実行する。

固定更新は60Hz。短いフレームは余り時間と押下・ジャンプを保持し、長いフレームは最大0.1秒を固定刻みに分割する。
押下とジャンプは複数刻みの最初に一回だけ渡す。Pauseとフォーカス喪失では未処理の押下を破棄する。
各固定コールバックの後に物理を進め、衝突イベントと固定段階のAnimator・IK・RootMotionコマンドを通常更新へ渡す。
AppとEditorの既存MovePlayers経路を使い、入力移動のないシーンでも固定処理があれば共通SceneEnvironmentから進める。

通常更新、Animator、ルートモーションの後にLateUpdateを呼ぶ。LateUpdateでの生成・削除・コンポーネント変更もリソース準備へ接続する。
進行済みAnimator状態を引き継ぎ、LateUpdate後に時刻を進めず姿勢・World IKを再評価する。
通常／Late段階の失敗時は両段階の候補を破棄し、固定段階の失敗時はバッチ全体のScript・シーン・物理・接触・インパルス・余り時間を巻き戻す。

検証: Debug・Development・Releaseのソリューションビルド、Debug／Releaseの全体回帰とScriptデータ・GPU実行検証が成功。
30fps／120fpsで同じ固定更新回数と物理位置、Pause・Step、押下の保持・解除、リソース失敗後のJoltの位置・速度復元を確認した。

## C++ゲーム処理の制作

ScriptModuleApiでC++処理を登録し、公開データと各更新段階を既存のScriptRuntimeへ接続する。
DLLは一時コピーから読み込み、コールバックがDLLの寿命を保持する。登録はモジュール単位で検証後に置き換え、組み込み・他モジュールの名前を上書きしない。
Editorの作成画面・外部編集・ソースとヘッダーの変更監視・自動コンパイル・診断・停止後の読み込みと、App起動時の読み込みを追加した。
配布では保存済みソースのスナップショットをコンパイルする。既存のBinをコピーせず、構成の違う古いDLLを持ち込まない。
コンパイル中にソース・ヘッダー・ファイル一覧が変わった場合は前のmanifestを維持して中止する。

検証: 3構成のソリューションビルド、Debug全体回帰、Debug／Releaseの実DLL登録・実行・再読み込み、ABI不一致と不正manifestの拒否、コンパイル失敗時のmanifest保持、ソースsnapshot、Release配布検証が成功。変更監視は実際のScriptAuthoringPanel::Pollを使うheadless検証が成功。ソース作成・変更からの自動コンパイル、Play中の読み込み保留、編集状態へ戻った後の読み込みとPlay許可を確認した。

## 汎用UI

InputField、Slider、Toggle、ScrollView、Mask、horizontal／vertical／gridのLayoutGroupを追加した。
保存・Inspector・Undoと、Editor Gameビュー／Appの入力・描画へ接続する。文字列・数値のbinding、変更／確定イベント、ScriptからのSetUiValue／SetUiTextも共通経路で反映する。
InputFieldのフォーカス中はゲーム移動を抑制し、ScrollViewとMaskは子孫の描画とヒットを制限する。

操作とデモは[汎用UI](UiControls.md)。回転したマスクのクリップは外接矩形を使う。ScriptModule ABIはversion 3となり、古いDLLは再コンパイルが必要。

## 追加シーンと保持オブジェクト

EditorとAppのSceneEnvironmentへ追加ロード・アンロードを接続した。Editorではロード済みシーンを編集でき、配置とシーン所属を保存・Undoする。
ScriptのLoadScene／UnloadSceneとUIボタンからも呼び出せる。追加時は衝突したIDと内部参照を変換し、存続するScript・Animator・音声の実行状態を保持する。
persistentを指定したオブジェクトと子孫はアンロードや置換ロード後も残る。親が削除される場合はWorld姿勢を保ってルートへ移す。

基本の編集・保存操作は[Editorの操作説明](../Editor/README.md)。保持物体のWorld姿勢をSRTで表せない場合は切り替えを拒否して現状を維持する。有効なメニューCanvasは統合したシーン全体で一つまで。

## 音響

AudioListener、階層を含む3D音源の位置・距離減衰・混合率・ピッチ、Master配下のミキサーグループを追加した。
音量・消音・低域フィルタ・リバーブを実際のXAudio2ボイスへ反映し、WAV／AACなどを少数のPCMチャンクで逐次再生する。
PauseはSourceの再生位置を保持し、全体出力も消音する。シーンの再構成では存続する音源を再利用する。

操作は[音響コンポーネント](Audio.md)。出力はステレオsubmix、リバーブはRoomプリセット。Streamingは元ファイルを保持し、各チャンクの上限は4 MiB。

## 描画規模

カメラ距離によるLOD、共有する静的メッシュのGPUインスタンシング、同フレームの深度とGPU query／predicationによる遮蔽カリングを追加した。
保存・InspectorとEditor／Appの描画へ接続し、Profilerには実際のバッチ数と全個体分の三角形数を反映する。

詳細は[描画・照明](Rendering.md)。LODは最大8段階でAnimator付きモデルは対象外。インスタンシングは不透明な静的個体を最大257個ずつまとめる。遮蔽判定を使う個体は個別描画する。

## 照明と色調整

PointLightの六面影、SpotLightの透視投影影、Materialの環境パノラマによるIBL、静的メッシュのライトマップベイクを追加した。
シーンの自動露出・色フィルター・コントラスト・彩度を線形HDRのポストエフェクトへ接続し、保存・Inspector・Undoに含める。

操作と制約は[描画・照明](Rendering.md)。局所影のアトラスは32面。IBLは画像mipとBRDF近似を使い、GGX反射プローブの事前積分は行わない。
ベイクは重なりのない既存UVが必要で、自動UV展開と間接光の反復計算は行わない。LDRライトマップでは1を超える照度を切り詰める。自動露出は毎フレーム測光し、時間的な明暗順応は行わない。

## ジャンル支援と物理

Terrainの高さブラシ・GPUメッシュ・Jolt三角形Collider、アトラス付きTilemapの描画・セル編集・厚み付きColliderを追加した。
NavMeshの高さ・傾斜・障害物BakeとA*を使い、NavAgentを60Hzで親座標を保って目的地へ移動する。保存・Inspector・Undoと複製／Prefab／追加シーンの参照変換へ接続する。

RigidbodyのXY平面拘束による2D物理、固定・距離・Hinge・SwingTwist Jointと制限を追加した。
ラグドールは現在の骨格からCollider・剛体・Jointを一括生成し、物理姿勢をGPUスキニングへ反映する。生成全体のUndoとアニメーション／物理制御の切り替えに対応する。

操作は[地形・Tilemap・ナビゲーション](Genre.md)、[ラグドール](Ragdoll.md)。NavMeshはセル面で、障害物Bakeはsolid BoxColliderを扱う。形状や障害物を変更したら再Bakeする。
Terrain／Tilemapは同じオブジェクトのMeshRendererと併設しない。ラグドールはスキンに使う一意な骨名とSRTで表せる姿勢が必要。