# カメラ・パーティクルの検証

Game.jsonの保存復元・状態によるモデル切り替え・無効ショートカットの拒否と、Appでのモデル／粒子／画像／UI描画をUiValidationで確認します。タイトルの開始先・ID変更・項目の無効化・選択順・演出時間・クリックによる登場スキップはValidateTitleAuthoringで検証します。ProjectSettingsValidationは一時フォルダーで設定保存復元と不正値・存在しないシーンによる保存失敗時の保持を確認し、ユーザーの設定は変更しません。
`WP1_AUTHORING_ONLY=1`を設定すると、今回のシーン・タイトル・プロジェクト設定に関するデータ検証だけをGPUなしで実行できます。描画の確認は通常の全回帰検証で行います。

## START／CONFIG／QUITタイトル（2026-10-07）

現行タイトルは上下／W・S／パッドでSTART・CONFIG・QUITを選び、Enter／Aまたはクリックで決定します。
CONFIG選択だけで歯車付き施設へ移動し、決定すると音量・背景演出・保存の設定を開きます。
Esc／Bで取消、START選択で通りへ復帰します。従来の2項目と任意ボタン方式は互換回帰の対象にのみ残っています。
TitlePresentationValidationはCONFIG到着、途中反転の位置連続性、START復帰と設定パネルをGPUで確認します。
画像はgenerated/title-rebuild/previews/config-focus.ppm・config-settings-1280x720.ppmなどに保存します。
WP1_TITLE_PRESENTATION_ONLY=1を設定すると、全回帰を省略してメニュー・タイトル演出とGPU画像だけを検証できます。
これは全回帰検証の代わりではなく、画面調整時の短い反復用です。
影は2048×2048の光源深度とPCFで毎フレーム計算します。地面の固定影素材は撤去しました。
ShadowValidationは直射光の遮蔽・環境光の保持、親移動・光源変更への追従、鏡映・薄い形状・垂直面への影をGPU読み戻しで確認します。
DebugではD3D12 InfoQueueで深度の書き込み／参照遷移と描画先復元も検証します。

キーフレーム演出はReviewRegressionValidationのAnimationValidationで検証します。
補間・ループ・未開始時計・不正値の拒否・JSON保存復元・Canvas拡縮下のクリック判定と、
Editorのカメラプレビュー・Pause・Step・開始時計・巻き戻し・編集データの保持を確認します。

## 静的解析と複雑度

リポジトリのルートで `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AnalyzeCode.ps1 -Label latest` を実行します。
cppcheck と lizard が必要です。PATH にない場合は `-Cppcheck` と `-Lizard` で実行ファイルを指定します。
Engine／App／Editor／SceneRuntime の自作コードを対象とし、外部ライブラリの診断は除外します。
cppcheck は C++20／win64 の warning・style・performance・portability を確認し、
lizard は CCN 15 超・100 行超・引数 8 個超を検出します。どちらかに指摘があればスクリプトは失敗します。
解析結果は `generated/analysis` に保存します。Windows SDK の完全なコンパイル検証の代替にはならないため、
変更後はビルドと以下の回帰検証も実行します。

リポジトリのルートを作業ディレクトリにして実行します。

```powershell
MSBuild tests/CameraParticleValidation.vcxproj /p:Configuration=Debug /p:Platform=x64
generated/tests/outputs/x64/Debug/CameraParticleValidation/CameraParticleValidation.exe
```

Release も同様に検証できます。WARP デバイスで、カメラの行列・不正入力、複数テクスチャグループの初期化、発生・寿命・上限を検証します。アプリの描画確認は別途 Debug / Development / Release で行います。
カメラは縦横比がほぼゼロの場合、近遠クリップがほぼ等しい場合、許容誤差の境界と直外の入力も検証します。拒否時に射影行列を維持することを確認します。

## 音声の検証

```powershell
MSBuild tests/AudioValidation.vcxproj /p:Configuration=Debug /p:Platform=x64
generated/tests/outputs/x64/Debug/AudioValidation/AudioValidation.exe
```

Release でも実行できます。Media Foundation で既存サンプル WAV を AAC（M4A）に変換し、デコード・無音再生・再生完了・ループ・停止・再初期化を確認します。読み込み後に元ファイルを改名しても再生できることを検証します。生成ファイルは `generated/tests/audio` に保存します。
通常 PCM／拡張形式 WAV のモノラル・ステレオと 8/16/24/32 bit、32 bit 格納幅に有効 24 bit を持つ音声も生成して検証します。正規化後の波形バイト列の保持、XAudio2 での無音再生、非標準チャンネル配置の拒否を確認します。

## シーン管理の検証

```powershell
MSBuild tests/SceneValidation.vcxproj /p:Configuration=Debug /p:Platform=x64
generated/tests/outputs/x64/Debug/SceneValidation/SceneValidation.exe
```

非表示ウィンドウと DirectX 12 デバイスを生成し、遷移予約・更新中に破棄されないこと・生成失敗時の旧シーン保持・終了時の破棄を検証します。Release でも実行できます。
アプリはタイトルから起動し、Enter でゲーム、ゲーム中の Escape でタイトルへ戻ります。

## コードレビュー修正の回帰検証

```powershell
MSBuild tests/ReviewRegressionValidation.vcxproj /p:Configuration=Debug /p:Platform=x64
generated/tests/outputs/x64/Debug/ReviewRegressionValidation/ReviewRegressionValidation.exe
```

Release でも実行してください。診断保存先が LocalAppData/WP1 になること、明示したログ・クラッシュ保存先が引き続き使えること、非表示ウィンドウでのタイトルの初期化・描画を検証します。診断ファイルの書き込み試験には generated/tests/diagnostics を使用します。

WARP 上のオクルージョンクエリで、通常の三角形と X／XY 鏡映した三角形が同じサンプル数を描画すること、裏向きの面は引き続き除去されることを確認します。Debug では利用可能な D3D12 InfoQueue のエラーも確認します。音量の取得・ミュート・再初期化後の状態は AudioValidation で検証します。

タイトルUIは全構成で Content/Assets/Textures/Title/Morning の Logo.png と StartBand.png を使用します。再生成する場合のみ PowerShell 7で scripts/GenerateMorningTitleArt.ps1 を実行してください。通常のビルドは生成済み画像をコピーします。未使用になった Title.png・UiAtlas.png・Caption.png・Slash.png は削除済みです。

タイトルの回帰検証では1280×720、1024×768、720×1280の非表示ウィンドウで初期化と描画を確認します。

Editorの配置処理は、実際のSceneWorldとEditHistoryを組み合わせて、複製→連続変形（鏡映を含む）→
Undo／Redo→保存→削除→Undo→再読み込みの順に検証します。配置全体・選択ID・保存済み判定を比較します。
保存先はgenerated/tests/layout-ioで、元のTitleStreet.jsonは書き換えません。
この検証はUIのマウス操作を自動化したものではなく、編集処理と描画の回帰検証です。
共有のEditStateを通した選択・編集要求の一度だけの取得・変形の適用と不正値の拒否・Undo／Redo時の選択復元・保存済み表示も確認します。
SceneViewportは原点のずれ・縦長へのリサイズ・領域境界・無効サイズについて、クリック座標と選択枠の座標変換を確認します。
RenderTextureは横長・縦長へのリサイズ、同サイズでの再利用、サイズ0と不正サイズでの保持を確認します。
連続フレームのクリア色をGPUから読み戻して両隅の画素を比較し、共有シーンのオフスクリーン描画も検証します。
Debugでは利用可能なD3D12 InfoQueueのエラーを確認します。

タイトル背景はTitleStreet.jsonのCamera・照明・Sky・ParticleEmitter・CameraSway・霧設定から作り、CC0の街並み・植生と汎用Mesh/Sky/GlowParticleシェーダーを描画します。ReviewRegressionValidation のタイトル検証ログは generated/tests/title-rendering.log に保存します。シェーダーは実行時にコンパイルするため、ビルド成功だけでなく Debug・Release の検証実行も確認してください。

設定の回帰検証は generated/tests/settings 以下に一時ファイルを作り、ユーザーの設定を変更しません。欠損・不正形式・上下限・上書き・保存失敗・取消と再編集・パッド接続直後の入力を確認します。設定UIの3選択状態と保存失敗文も上記3サイズで描画します。

実機確認ではタイトルの「設定」を開き、左右で音量と背景演出を変更して「保存して戻る」を選びます。アプリ再起動後の復元と、ゲーム開始後にSpace／Aで再生するサンプル音の音量を確認してください。Esc／Bは保存せず戻ります。背景演出OFFでカメラが停止し光の粒が消えること、ONで再開すること、取消で元の設定へ戻ることを確認してください。

タイトル演出の回帰検証は、登場スキップと決定を分離し、キーを離して押し直すまで開始しないこと、非アクティブ中の時間停止、画面を覆った後の一度だけの遷移通知を確認します。登場・ワイプ途中の描画も3サイズで実行します。実機では初回起動、登場中のEnter／A、開始、ゲームからの再訪（登場省略）、終了を確認してください。

背景待機演出は150秒分のシーンに設定したカメラ位置範囲・粒の不透明度を検証します。OFFと非アクティブ時の停止、不正時間の無視、3サイズでのON／OFF／再開描画も確認します。

タイトル音声の回帰検証は5つのWAVの読み込み・無音ループ再生・停止と、メニュー効果音イベントの重複抑止を確認します。実機では音量設定の即時反映と取消、0%の無音、フォーカス復帰時のBGMの続きからの再開、ゲーム遷移・終了時の停止を確認してください。音源再生成は scripts/GenerateTitleAudio.py を実行します。

DebugのRenderTexture検証ではScene用SRVの枠がリサイズ後も同じであること、ドッキング後の画像領域、
レイアウトの保存内容も確認します。初回配置の測定フレームの次から画像領域を検証します。
保存先はgenerated/tests/editor-layoutで、実際のEditorのレイアウト設定は変更しません。

Inspectorの名前変更（日本語を含む）・不正な名前の拒否・Transformリセットを実際のSceneWorldで検証し、
Undo／Redoの順序と選択ID、保存と再読み込み後の配置全体を比較します。

ProjectCatalogの検証はgenerated/tests/project-catalogに小さなファイルを作り、対応形式の抽出、
フォルダー構造、全フォルダー検索、大文字拡張子、種類の識別、再走査失敗時の一覧保持と復帰を確認します。

ProjectからSceneへのモデル配置について、地面との交点、空・水平線の代替位置、画像外の拒否、パネル移動・サイズ変更後の座標を検証します。

Scene／GameのUIディスクリプターの分離・リサイズ時の安定性・範囲外スロットの拒否と、標準ドックへのGame配置を検証します。

Consoleの500件上限・時系列・初期化前の記録・種類と検索のフィルター・UTF-8と複数行・Clear後の診断ファイル保持を検証します。

シーンの切り替えについて、未保存確認とキャンセル、読み込み失敗時の配置・保存先保持、新規シーンの未保存状態と保存、既存ファイルの保護、日本語名を検証します。

別名保存の新規ファイル作成、確認前の上書き拒否、確認後の置き換え、書き込み失敗時の保存先保持、元シーンの保持を検証します。

複数選択のCtrl追加・解除、主選択の引き継ぎ、検索結果のShift範囲、重複の除去、選択変更による未保存・Undoへの影響、履歴からの複数選択復元を検証します。

複数選択の一括移動について、全対象の位置差、回転・拡縮の維持、描画用境界とピッキングの更新、ドラッグ一回のUndo／Redo、不正ID・重複・非有限値による部分反映の拒否を検証します。

親IDの旧形式互換・UTF-8・前方参照・保存復元、不在の親・自己参照・循環・型不正の拒否、深い階層、子の追加／複製と親削除時のルート化・Undo復元を検証します。

Hierarchyのツリー順・折りたたみ・深い階層、親変更とルート化、不正な親子付けの原子的な拒否、同じ親への変更、親変更のUndo／Redoを検証します。

Debug構成では実際のObjectPanelを親子のあるSceneWorldで描画し、Hierarchy初回描画・連続フレームのImGui境界アサーションを検証します。

SceneTransformsのローカル／ワールド合成、親の回転・非一様拡縮・鏡映、せん断を保つ逆変換、旧ワールド形式、階層順、深い階層、不正グラフ／行列による出力保持を検証します。

親Transform描画の検証はversion 2のローカルSRT保存復元、子孫の選択枠追従、せん断行列の描画、親子同時移動、
親回転・拡縮下のワールド移動と複製、親変更・削除時の配置保持またはせん断の安全な拒否を確認します。
旧version 1および座標系タグ付きシーンは通常の読み込みでは拒否します。
Transform APIは親に対するローカル設定、ワールド設定、変換不能時の描画・データ保持、不明IDでの出力保持を確認します。
旧形式の独立した変換ツールは`python tests/test_scene_conversion.py`で検証します。
回転・反転した親と順不同の多段階層、全ワールド行列の配置保持、ローカル形式の二重変換防止、
せん断時の変更拒否、不正な階層・数値、空および2,000段のシーンを対象にします。

階層操作の統一では回転・反転した親同士の変更と孫の配置保持、同じ親への設定の完全な無変更、
せん断した描画行列の下での複製とローカル回転・拡縮保持、複数の子の途中で失敗した削除の全体取消、
直下の子だけのルート化と孫の親子関係、親削除のUndo/Redoを確認します。

ギズモ計算は親の反転・非一様拡縮下での直交した操作軸、実際のワールド中心、Local/World回転軸、
変更していないローカル成分の完全な保持、負の拡縮、ゼロ拡縮・不正行列の拒否、親子同時選択移動を確認します。

編集確定の検証は入力欄の切り替えによる履歴分割、ドラッグ一回のUndo、複数選択の保持、
途中の入力失敗による最終有効結果の保持、失敗・無変更操作での変更状態とRedo保持、保存時の履歴確定、
モデル作成失敗時のID予約取消を確認します。

最終の通し検証は新規シーン作成から三段階層、複数選択ドラッグ、Undo/Redo、保存、複製、親削除、
別名保存・再オープン・描画までを同じドキュメントと履歴で確認します。行列の検証・分解テストはSceneTransformsを直接使用し、
旧Editor行列ヘッダーは削除しています。非有限値の行列が一致扱いにならないことも確認します。
手動確認手順はEditor/README.md末尾を参照してください。

複数選択フォーカスは反転した二つのオブジェクトの全ワールド境界が画面内へ収まること、
カメラの向きとシーン内容の保持、空選択・存在しない対象でのフォーカス拒否を確認します。

複数削除は要求時の選択IDの保持、選択された親子の同時削除、残す孫のワールド配置とルート化、
Undo/Redoと複数選択・変更状態の復元、不明ID・変換不能な残存対象での全体取消、ID重複の正規化、
要求後の別選択の維持、削除対象のせん断の分解回避、空選択の拒否・全件削除を確認します。

複数複製は子が親より先に選択された場合、負・非一様スケールによるせん断、コピー親子のID再接続、一度だけのワールドオフセット、未選択の子孫と親の扱いを検証します。
コピー全体の選択と主選択、Undo/Redo、重複ID、空選択、無効ID・非有限オフセットの一括拒否、失敗時のID採番維持も検証します。

複数回転は子を主選択とするピボット、親子の二重適用防止、未選択子孫の追従、負スケール、Local/Worldハンドルの回転差分を検証します。
無操作・無効行列と選択・保存不能なローカルせん断の一括拒否、変更状態とRedo維持、Undo/Redoによるシーンと選択復元も確認します。

複数拡縮は主選択ピボット、一様・Local軸拡縮、間隔と形状の変換、親子の二重適用防止、未選択子孫の追従、既存の負スケール維持を検証します。
せん断下のハンドル倍率、無操作、無効選択・倍率・軸行列と保存不能なせん断の一括拒否、失敗時の選択とRedo維持、Undo/Redoも確認します。

PlayStateはEditing/Playing/Pausedの状態遷移、状態ごとの編集可否、Playingだけの時計更新、Pause/Resumeの継続、両実行状態からのStopと次回開始時のリセットを検証します。
重複コマンド、非有限・非正のフレーム時間、累積時間のオーバーフローで状態と時計が破損しないことも確認します。GameSessionの実行接続は下記で確認し、コマ送りは後続の段階です。

GameSessionは未保存配置の独立コピー、実際のカメラ・粒の更新、非アクティブ・無効時間・Pause時の凍結、Resumeで同じ実行用シーンを継続することを検証します。
Game用RenderTextureへの描画とGPU完了後のStop、初期化失敗時のEditing維持、空シーンと再開始時の演出リセット、保存ファイルを変更しないことも確認します。Appのタイトル背景の既存描画テストも共通化後に継続します。

StepはPausedでだけ固定1/60秒・更新一回を受理し、繰り返し実行とResumeが時計を維持すること、Gameの粒が一度だけ進み描画・通常更新では進まないことを確認します。

PlaySnapshotは配置・複数選択と主選択・未保存状態・保存先・保存基準を含むUndo/Redo分岐の一括復元、再構築失敗時のデータ維持、変更のない配置での読み込み省略、新規未保存シーンの復元とファイル非書き込みを確認します。

Projectの種類別分類・Shadersルート・大文字拡張子・失敗時のカタログ維持、アセット参照からScene選択へ戻るInspector状態、サイズと制限付きソースプレビュー、バイナリ・不正パス・読み込み失敗を確認します。


## シーンの環境Component

ReviewRegressionValidationはDebug／Releaseの両方で、Camera・DirectionalLight・Sky・ParticleEmitter・CameraSwayと背景・霧の保存、カメラのXYZ回転・親Transform継承、粒の発生位置と軌道、長時間の揺れ、OFF・非アクティブ・不正時間での停止を確認します。

EnvironmentValidation.hは64×32のGPU描画を読み戻し、Skyの青→緑の即時編集、Gameと編集プレビューの色一致、無効Skyに固定背景が残らないこと、霧の色／有効切り替えを検証します。MeshRendererの直接描画では大きい／小さいワールド拡縮でも法線がオーバーフロー／アンダーフローせず照明が一致することを確認します。環境編集のドラッグを一回のUndoへまとめ、Redo・全Componentを含む保存／再読み込み・複製・主Cameraの削除／Undoも確認します。既存のPlay／Pause／Step／Resume／Stop・スナップショット・アセット更新の回帰検証を継続します。

ConvertSceneToLocal.pyはversion 4の設定とComponentを維持し、すでにローカル座標のシーンを二重変換しません。`python tests/test_scene_conversion.py`で検証します。画面上の入力と見た目はEditor/ManualChecks.mdのK〜Mを別途手動で確認してください。


UI・音声・JSONの回帰はReviewRegressionValidationに含みます。Canvasの縦横比、アンカー・ピボット・親の回転、条件表示、状態代入、Component保存、UIドラッグのUndo／Redo、複製時の参照変更を検証します。GPU読み戻しで単色Image・ボタンHover・非表示・Unicode Textを確認し、CameraなしのUI描画と失敗時のリソース保持も検証します。音源はミュートして自動再生・Pause保持・Stop・失敗時の停止を確認します。
AudioValidationは有限長音源をPauseしたまま本来の再生時間を超えて待ち、バッファー保持とResume後の終了を検証します。自動テストは聴感や手動のUI操作確認とは別です。ManualChecks N〜Pは未確認として扱います。

シーンと設定のC++ JSON処理はnlohmann/json 3.12.0です。旧シーンの型検証・不正入力・Save/Loadと旧設定テキストの読み込みを維持し、JSON設定の型誤りも拒否します。依存ヘッダーの出典・SHA-256はEngine/externals/nlohmann/README.mdに記録しています。


EditorのImGuiフォントはDebug回帰のEditorFontValidationで確認します。欠損時にアトラスが変わらないこと、初期化の重複防止、Fira MonoとM PLUS 1pの合成、ASCIIの等幅、ひらがな・カタカナ・半角カタカナ・漢字の字形とラスタライズ、UTF-8入力欄への日本語入力、DX12動的フォントテクスチャの描画を検証します。非表示の検証ウィンドウを使用します。ユーザーが操作中のEditor画面を制御しません。IME候補ウィンドウの位置と実際の変換操作はManualChecks Qで手動確認します。

日本語UIでは、ドッキングの標準メニュー登録、旧英語ウィンドウ名と選択タブIDの移行、ドックノード保持、移行の再適用で変化しないことを同じ検証で確認します。PlayStateの表示名は編集中・再生中・一時停止中を検証します。画面上の文言とボタンの操作はManualChecks Rで手動確認します。


TitlePresentationValidationはタイトルのGPU画像をgenerated/title-rebuild/previewsへ保存します。
横長・4:3・縦長で開始案内の位置と最終遷移の四隅の被覆を検証し、待機OFF・巻き戻しを確認します。
WP1_TITLE_PREVIEW=1を設定してReviewRegressionValidationを実行すると、登場・待機・開始の81フレームをPPMに保存します。
生成画像は検証用で、通常ビルドや保存済みシーンには影響しません。

QUITは選択だけでは終了せず、決定後にExitを一度だけ通知することを検証。QUITへの途中切替・左ビル到着と3画面比率で全ボタンの収まりも確認します。

PlayerControllerはAnimationValidationの再生検証でJSON保存復元、不正な速度の拒否、斜め移動の速度補正、不正な更新時間の拒否、長いフレームの移動量上限と編集データの保持を確認します。Debug／Releaseの全回帰検証に含まれます。

PhysicsValidationはGPUなしでBoxColliderとPlayerControllerの保存復元、旧シーンの重力OFF、不正な箱サイズの拒否、落下・接地・ジャンプ・空中ジャンプの拒否、薄い壁のすり抜け防止、無効Component、親の拡縮と不正な時間・入力を検証します。Debug／Releaseの全回帰に含まれ、`WP1_PHYSICS_ONLY=1`で単独実行できます。PhysicsPlayground.jsonの操作はEditorで確認できます。
