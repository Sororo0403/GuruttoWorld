# 検証の実行と構成

実行スクリプトと `.vcxproj` はこのフォルダー直下に置き、既存の実行コマンドを維持しています。C++ のテスト本体は `src/Animation`、`Architecture`、`Assets`、`Gameplay`、`Physics`、`Rendering`、`Scene`、`Scripts`、`Ui` に分類しています。通し回帰の入口は `src/Regression`、共通の計測・fixture ヘッダーは `src/Support`、DLL 用のソースは `fixtures` です。結果は従来どおり `generated/tests` などへ出力します。

## 構造と確定境界の回帰（2026-10-23）

`./tests/TestArchitectureStructure.ps1` は Debug / Development / Release の構造契約・既存148契約・GPU監査回帰、Editorとゲームのビルド、実DLLと独立Schema生成を検証します。
シーンを一度取得した後の外部変更・読み取り不能、編集と実行の所有分離、Script定義の独立性、シーン操作の結果通知、GPU同期、DLL公開のプロセス間排他と失敗時保持を対象にします。
Editorのプロジェクト検証とゲームのパッケージ検証も実行します。`-SkipGpu` はGPU実行、`-SkipHosts` はEditorとゲームの構築・実行を省略します。
ログは `generated/architecture-fixes/validation-*` に保存します。設計と保証範囲は `docs/ArchitectureFixes-2026-10-23.md` を参照してください。
`SceneExecutionProfile.vcxproj` はReleaseでWorld更新の基準計測を行います。描画を含めずCSVを出力し、時間を合否判定には使いません。

通常回帰の前に、同じ構成の`ReviewRegressionValidation`をビルドし、`./scripts/BuildScripts.ps1 -Configuration Release -Content Games/GuruttoWorld`でソース側のゲームDLLも更新してください。Debugでは両方の構成名をDebugへ変更します。通常回帰の共通再生検証はソース側のDLLを読み込むため、ビルド出力側のDLL更新だけでは古いABIが残ります。

`WP1_TITLE_SIZES_ONLY=1` を設定して `ReviewRegressionValidation.exe` を直接実行すると、今回分離した編集・履歴・保存の検証を1回行い、同じ編集結果を1280×720・1024×768・720×1280で描画します。各サイズでタイトル・背景演出・設定UIも描画し、編集とサイズ別描画の所要秒数を出力します。通常回帰でも同じ分離を使用します。`RunRegression.ps1` は単独検証用の環境変数を解除するため、このモードでは実行ファイルを直接起動してください。

テクスチャのミップ・圧縮結果は、元の全画素と寸法・sRGB・サイズ制限・圧縮・ミップ設定を照合して共有します。保持するCPUデータは192MiB・128件を上限に古い項目から退避し、GPUリソースは各呼び出しで生成・転送します。通常回帰の `TextureImportValidation` は共有の実行回数、元データの編集との分離、全設定と寸法の相違、不正設定の拒否、最大ミップと縮小描画の結果一致も確認します。単独実行は `WP1_TEXTURE_IMPORT_ONLY=1` を設定して検証実行ファイルを起動します。`TextureImportCacheValidation.vcxproj` の単独実行では、実素材に近い約149MiBの変換結果を2回使用し、全件が再利用できることも確認します。

通常回帰は `powershell -NoProfile -ExecutionPolicy Bypass -File tests/RunRegression.ps1 -Configuration Release` で実行します。Debugも同様です。実行フェーズと経過秒数、プロセス別のログを記録し、既定の上限は1800秒です。固定120秒で打ち切らず、タイトルのApp／Editor比較と実際の開始先シーンまで完走させます。`-TimeoutSeconds`で上限を変更できます。
`tests/TestRegressionRunner.ps1`は実プロセスで成功・終了コード7・検証モードの隔離と復元・期限超過時の子プロセス終了を確認します。1秒の上限に対して1.5秒で終了するケースも、確実に期限超過として拒否します。

`tests/TestSharedContent.ps1 -Configuration Release` は新しい出力先へ素材をコピーし、現行ゲームのシーン・音声・Script・Shaderの内容一致を確認します。`SunriseStreetValidation`の`WP1_SOUND_LIFECYCLE_ONLY=1`は、独自音声の開始失敗、Pause、Step、再開、フォーカス喪失と復帰、停止時の解放を実際の出力音量で検証します。Scriptの候補破棄と描画準備失敗は通常のScript回帰に含みます。

静的モデルの変換キャッシュは通常回帰の `ModelImportCacheValidation` で検証します。同じ入力の共有、呼び出し元のメッシュ編集との分離、同じサイズ・更新日時に戻したOBJ／MTL変更、倍率変更、MTLの削除・追加、不正モデル時の失敗と出力保持を確認します。テクスチャ参照は共有結果からも毎回解決します。 128件の作業集合の連続再利用と、上限超過時に最近使った項目が残り、最も古い項目だけが再取り込みされることも確認します。単独検証は `ModelImportCacheValidation.vcxproj` を対象構成でビルドし、対応する `ModelImportCacheValidation.exe` を実行します。タイトル演出・設定遷移・再生一致の各シナリオは `Timing:` 行に実時間を出力します。 初期化・入力更新・状態比較・ログ確認は実行回数と合計時間も記録します。`WP1_TITLE_PRESENTATION_ONLY=1` と `WP1_INITIALIZATION_TIMING=1` の単独実行では、初期化内のCPUプロファイルも出力します。この詳細計測時はプロファイラーのサンプルを読み出します。

通しプレイのCPU検証は、実行時と同じ候補更新・差分検証を使います。最初に全体を検証し、その後も物理更新とテスト側の直接編集を含む全変更を、直前の確定状態と比較します。Script API自体の失敗時の復元は、通常のScript回帰で別に検証します。

## 全体設計監査の再発防止検証

`ArchitectureAuditFixValidation`は通常回帰にも含めています。単独実行は次のコマンドです。`Development`・`Release`も同じ構成名で実行できます。

```powershell
MSBuild tests/ArchitectureAuditFixValidation.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:PreferredToolArchitecture=x64 /m:1 /nr:false
generated/tests/outputs/x64/Debug/ArchitectureAuditFixValidation/ArchitectureAuditFixValidation.exe
```

未知Script・Start例外・開始後の描画／UI準備失敗による単独／追加ロードの拒否、PersistentなScriptの継続と終了回数を確認します。フレーム更新はFixedUpdate後のUpdate・LateUpdate・UI準備失敗を注入し、配置・物理・Script内部状態・入力蓄積・時計・UI・音声・終了処理と再試行を照合します。保存は設定の外部変更・削除、一括保存の後段競合、ファイルロックによる途中失敗、別名保存のパスと保存基準の復元を検証します。手続きメッシュは利用中の共有、内容変更、32個の異なるIDの解放とGPUフェンス後の退役を確認します。素材・保存先・一時ファイルは`generated/tests/architecture-audit-fixes`へ分離します。

再監査の4件は`ReauditFixValidation.h`から同じ専用実行ファイルと通常回帰で検証します。終了準備の有効な書き戻し後のUI失敗・例外、未知Script・不正パラメーターの拒否、資源を保持した再試行と終了回数、同じScript IDの振る舞い交換順序を確認します。保存は元の64MiB候補による同時編集の再現に加え、保持中の書込・置換・削除の拒否と、ドキュメント確定失敗の復元を確認します。

保存の追加監査3件は`FileSaveRecoveryValidation.vcxproj`で検証します。実装をコピーせず、成功したWindows APIの直後に子プロセスを強制終了します。一覧公開・両ファイルの退避／設置・確定・掃除・巻き戻し・復旧中断の各境界、新規作成／削除とバイナリー内容、設定読込前の自動復旧、別プロセスの空白区間への作成、外部版と候補の保管、復旧先のロック、領域外を指す不正記録を確認します。メタデータは2プロセスで100件を同時登録し、返却IDの一致・再索引後の参照解決・既存設定保持と古い編集の拒否を検証します。結果は`generated/tests/file-save-recovery`へ分離します。`ReviewRegressionValidation`のプロジェクト参照でビルドし、通常回帰の`FileSaveRecoveryValidation`段階で毎回実行します。

`FileSaveApiValidation`は別プロセスでScene／Materialの公開保存APIとプロジェクトのOpen／Scanを実行します。先行復旧後の保存内容と上書き禁止を確認し、保存完了記録・掃除の失敗、完了後の外部編集／削除、旧形式2の読込、48境界の復旧中再終了、入れ子のAssets／Shadersと旧保存領域の探索も通常の保存回帰に含めます。形式3の完了記録後は、対象ファイルへ内容を再適用せず作業ファイルだけを回収します。

Script SDK ABI 14では、配置の復元などは`prepareStop(ScriptContext&)`へ移します。この処理は候補データだけを変更し、音声停止などの外部副作用を起こしません。`stop(const ScriptStopContext&)`は確定後の資源解放、または失敗したStartの後始末に使用します。旧DLLはABI照合で拒否するため、ホストと同じ構成で再ビルドしてください。

## エディター／プレイヤーの共通再生検証

`GamePlaybackFocusedValidation` は実際の `App::AuthoredScene::Update` と `Editor::GameSession` に同じ診断入力を渡し、毎フレームのシーン・UI値・Transform・Materialを比較します。CONFIGの設定シーンへの切り替え、取消・保存による帰還、START、クリック、一時停止・コマ送り・再開、ゲームパッドの入力元、編集した接近先・時間、スクリプトによるシーン差し替えと旧操作の破棄、QUIT、Material強度だけの変更の保存・Undoを検証します。

リポジトリのルートで、DevelopmentのEngine／SceneRuntimeと一致するゲームDLLを先に生成してから実行します。

```powershell
MSBuild tests/GamePlaybackFocusedValidation.vcxproj /p:Configuration=Development /p:Platform=x64 /p:PreferredToolArchitecture=x64 /m:1 /nr:false
./scripts/BuildScripts.ps1 -Configuration Development -Content Games/GuruttoWorld -OutputContent Games/GuruttoWorld -WorkRoot Games/GuruttoWorld
./generated/tests/outputs/x64/Development/GamePlaybackFocusedValidation/GamePlaybackFocusedValidation.exe
```

GPUで描画した両者の接近中・設定画面・帰還中の画像は `generated/tests/playback-captures` に保存します。設定の保存先は検証用ディレクトリに分離します。
`TitleDepartureValidation` はSTART／QUITの色、現在カメラからの出発、道路・看板矢印の方向、Pause／Step／Resume、最終描画前のロード・終了抑止を追加検証します。白黒化の途中とカメラ到着時のGPU画像も同じディレクトリへ保存します。
色や配置を調整するときは検証実行ファイルに `--departure-only` を渡すと、STARTのApp／Editor比較とSTART／QUITの描画・移動だけを実行できます。

## 静的解析（2026-10-13）

`scripts/AnalyzeCode.ps1 -Label authoring-final` は、Engine・SceneRuntime・App・Editor・C++ゲーム処理の全101ソースをCppcheck 2.21.0で解析します。MSVCのDebug・Development・Releaseの定義をそれぞれ指定し、Jolt・Assimpの実際のインクルード先を使用します。外部ライブラリ自体の診断は対象外です。4並列と構成別キャッシュで再検査でき、結果は`generated/analysis/cppcheck-authoring-final-<構成>.txt`に保存します。構成を指定しない解析はJSONライブラリの未使用条件の組合せを検査して長時間化するため、実際のビルド条件を明示しています。

修正後のCppcheck診断は3構成とも0件です。値渡しによるコピー、構造体の既定値、重複条件、検証・変換ループを見直しました。シェーダーincludeは保持用コンテナーへ格納してから呼び出し元へポインターを返します。DLLの寿命を保持する`ModuleCallback::lifetime`は削除せず、用途をコメントに記載して当該メンバーの`unusedStructMember`誤検出だけを抑制しています。

Lizard 1.23.0の基準は分岐数15・行数100・引数数8です。これを超える関数は修正前130、修正後129で、`generated/analysis/lizard-authoring-final.txt`に全件を記録します。UI項目・JSON種別の分岐、既存描画APIの引数などへの保守性の注意であり、これだけで実行不具合を示すものではありません。閾値の超過は残るため、スクリプトの終了はLizardの注意を理由に失敗として報告します。Cppcheckの診断とは別の結果です。

AuthoringSettingsValidationは全既存シーンの保存復元、物理・影・ビュー品質・入力・シェーダー設定、海の品質・形状、追加型付きデータ、任意のAnimation時計と対象、Editor Playのメニュー保存・復元・失敗表示、共通重力の実行反映を検証します。ReviewRegressionValidationの通常実行と`WP1_AUTHORING_ONLY=1`に含まれます。ParkAdventureValidationは変更したHP・UI文言・改名した参照対象・戻り先に加え、既存公園の通しプレイを確認します。OceanSunsetValidationは低品質設定・太陽光・シェーダー選択をD3D12描画で検証します。

`WP1_ALL_SCENES_ONLY=1`を設定してParkAdventureValidationを実行すると、全25既存シーンを個別に保存・再読込・初期化し、6フレームずつ物理・ゲーム処理・アニメーション・音声・GPU描画を実行します。実行中の警告・エラーとD3D12のエラーも検査し、元のシーンが変更されていないことを確認します。長い操作シーケンスは公園の通しプレイと個別機能の回帰検証で補います。

ScriptModuleValidationはモジュール単位の登録、名前競合と不正設定時の既存登録保持、ソース作成と上書き拒否をScriptValidationに含めます。
実DLLの確認は `WP1_SCRIPT_MODULE_SETUP=1` で検証用ソースを作成し、`scripts/BuildScripts.ps1 -Configuration Debug -Content generated/tests/native-script-project/Content` でコンパイルした後、`WP1_SCRIPT_MODULE_ONLY=1` で実行します。
この検証には同じ構成でビルドした`Games/GuruttoWorld`のDLLも使用します。作業フォルダーを別プロジェクトへ変更し、一時コピーしたDLLが登録元ゲームの音声を再生できることを確認します。SDK ABI 8では、すべてのDLLコールバックへ`ScriptContext::contentRoot`で登録元の絶対素材ルートを渡します。`WP1_SOUND_LIFECYCLE_ONLY=1`は実行中の`clip`変更後の再生も検証します。
Releaseも同じ構成のDLLを用意します。ホストContextでの移動、再読み込み後の古いコールバックの寿命、ABI不一致・パス逸脱・読み込み失敗時の登録保持を検証します。
`tests/TestScriptBuild.ps1` は意図したC++エラーを検出し、診断の公開と前のDLL manifestの保持を確認してソースを戻します。

ScriptValidationは型付きデータの保存復元、公開型の検証、内部データの失敗時復元、削除済み参照と、階層・Prefab・Editor複製の参照変換も検証します。
ScriptPhaseValidationは更新段階の順序、60Hz物理の30／120fps比較、Pause・Step、短いフレームの押下保持とPause・フォーカス喪失での解除、固定バッチの物理・内部状態復元、LateUpdateのリソース失敗を検証します。
`WP1_SCRIPT_RUNTIME_ONLY=1`には組み込みFollowTargetの実行とEditorのUndoによる保存データ・参照の復元を含みます。

`WP1_SCENE_UI_ONLY=1`でReviewRegressionValidationを実行すると、SceneのCanvas平面のGPU描画・深度判定・表示切り替え・視点移動・平面の移動／回転／拡縮・クリック座標・UIドラッグ・Fフォーカスと、既存のGame UI描画・UI編集を検証します。

MenuAuthoringValidationは、任意のメニュー番号と遷移先、4行以上の設定、独自キーの保存・取消・保存失敗、音量の範囲と小数の刻み、状態出力、カメラ時計、操作音、Undo／Redo、シーン改名後のランタイム選択、右スティック・トリガー・マウスの共通入力を検証します。WP1_AUTHORING_ONLYにも含まれます。タイトルの描画検証では、スクラブ・実行中の遷移が保存した倍率・加算・上下限と独自の出力キーを使うことも確認します。

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

タイトルUIは全構成で Games/GuruttoWorld/Assets/Textures/Title/Morning の Logo.png と StartBand.png を使用します。再生成する場合のみ PowerShell 7で scripts/GenerateMorningTitleArt.ps1 を実行してください。通常のビルドは生成済み画像をコピーします。未使用になった Title.png・UiAtlas.png・Caption.png・Slash.png は削除済みです。

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

`WP1_RIGID_ONLY=1`ではJoltの剛体保存復元、落下・接地、質量付きインパルス、物体同士の反発、CCD、レイヤーの拒否、TriggerのEnter/Stay/Exit、Raycastの距離・法線・マスク・Trigger選択、カプセルのジャンプ・斜面・移動床、凹メッシュと動的凸包、不正設定時の保持を検証します。`WP1_RIGID_RUNTIME_ONLY=1`では上記と、実際のRigidBodyPlayground描画、Inspectorのスタック、Pause・Step・再開始、ScriptのRaycast・インパルス・衝突イベント、編集データ保持も確認します。

ScriptValidationは独自C++処理の登録、重複・不正なメタデータの拒否、複数処理の保存復元、開始・更新・無効化・再有効化・終了、不明な処理の設定保持と実行エラーを確認します。Debug／Releaseの全回帰に含まれ、`WP1_SCRIPT_ONLY=1`で単独実行できます。

ScriptValidationは検索・型付きComponent取得、順不同の親子複製、宛先付きイベントの次フレーム配信、子孫削除、不正生成時のシーン保持も確認します。`WP1_SCRIPT_RUNTIME_ONLY=1`ではGPUを使い、実行中のモデル生成・削除、UI準備、モデル読み込み失敗時の保持、Script内部stateの継続と失敗フレームの取消を検証します。

PrefabValidationは親子とIDの生成、リンクの保存復元、元データ変更の反映と個別編集の保持、新しい子の追加、ファイル欠損時のシーン保持、リンク解除、抽出と不正パスの拒否を検証します。Debug／Releaseの全回帰に含まれ、`WP1_PREFAB_ONLY=1`で単独実行できます。

同じComponent内の未編集プロパティの継承、上書き一覧、プロパティ単位の復元・元ファイルへの適用、全体復元時の配置保持、Variant・入れ子の継承と再抽出、循環依存の原子的拒否とエラー後の再利用も検証します。

InputActionValidationはキー・パッド・スティックのAction値と押下、保持中の非反復、しきい値、非アクティブ時の解除、復帰時の押下抑止、再割り当て、不正キーの拒否、独自処理とButtonへのAction入力を検証します。ProjectSettingsValidationはAction設定の保存復元も確認します。`WP1_INPUT_ONLY=1`で単独実行できます。

MaterialValidationはMaterialとMeshRenderer参照の保存復元、不正値の保存失敗時の既存ファイル保持と、GPU読み戻しによる色・透明度・画像の上書きを確認します。D3D12のルート定数は64DWORD以内に収め、既存の鏡映・法線・影の回帰も継続します。`WP1_MATERIAL_ONLY=1`で単独実行できます。
PBRの正面照明をGGXの解析値と比較し、金属の反射色・粗さの変化、法線マップとY反転、退化UV、照明OFFもGPU画素で検証します。旧Materialの互換読み込み、法線画像IDの改名追従、不正パスの拒否、RenderingPlaygroundのApp共通環境とEditor再生での描画・停止時の編集データ保持も含みます。

`WP1_LIGHTS_ONLY=1`ではPoint/Spotの保存復元、Componentコピー・ID、不正値と32灯上限、GPU画素による逆二乗減衰・距離制限・表裏・円錐の内外とフェード・色の加算・32灯・PBR・照明OFF・極端に遠い光源での数値保持を検証します。同じフレームの後続アップロードが先の描画を上書きしないこと、複数フレームのリソース寿命、親のカメラに追従するSpot、動くPoint、App共通環境・Editor再生・Inspector・停止時の編集データ保持も含みます。

`WP1_POST_ONLY=1`ではHDRのFP16画素読み戻しでPBRの1を超える明るさ、鏡映・透明描画、スプライトの線形変換、パーティクルの加算を検証します。LDR画素では露出、Reinhard/ACES近似/sRGB変換、ブルームのしきい値と広がり、元の描画先の復元、GPUが処理中のリサイズも確認します。旧シーンで無効となる互換性、不正値・初期化失敗時の保持と再試行、空への処理、UIが露出を受けないこと、PostEffectsPlaygroundの実際の再生・影・PBR・パーティクル・UIと停止時の設定保持、Inspector、Undo/Redo、保存・再読み込みを含みます。大きい回帰テストのオブジェクトファイルは`/bigobj`で生成します。

AnimatorValidationはglTF読み込み、ボーン変形・法線、補間・ループ・状態遷移、GPU更新、複製・削除と不正クリップ時の保持を確認します。`WP1_ANIMATOR_ONLY=1`で単独実行します。AssetDatabaseValidationは参照ID保存、改名後の旧パス・ID解決、倍率・UV反転、上書き・パス逸脱・ID欠損の拒否を確認します。`WP1_ASSET_ONLY=1`で単独実行します。

ProfilerValidationはCPU描画時間・フレーム数と、完了フェンスに同期したGPUタイムスタンプの読み戻しを確認します。`WP1_PROFILER_ONLY=1`で単独実行します。`scripts/BuildPlayer.ps1 -Configuration Release` は配布物を生成し、そのAppを `--validate-package` で起動して実際のリソース読み込み・描画を検証します。package.jsonに同梱ファイルのサイズ・SHA-256を記録します。

## 公園ステージの検証

`python -m unittest discover -s tests -p test_park_generation.py -v`で100個のSeedと不正なコースの拒否を確認します。
`MSBuild tests/ParkAdventureValidation.vcxproj /p:Configuration=Release /p:Platform=x64`で専用検証をビルドし、
`generated/tests/outputs/x64/Release/ParkAdventureValidation/ParkAdventureValidation.exe`をリポジトリルートから実行します。
実際のJolt物理とゲーム処理でジャンプ・空中停止・戦闘・全収集・落下復帰・スイッチ・クリア・カメラ・再挑戦を確認し、入口と水路の描画画像を保存します。

`$env:WP1_PARK_STUTTER_ONLY='1'`を設定してReleaseのParkAdventureValidationを実行すると、静止・取得・空中停止の実描画付きフレーム計測と144Hz相当の位置更新検証を行います。`generated/park/stutter.csv`と`motion-144.csv`に記録し、完了後は`Remove-Item Env:WP1_PARK_STUTTER_ONLY`で解除します。測定値に合否しきい値は設けず、原因の比較に使用します。内訳と制約は`docs/ParkAdventure.md`に記載しています。


`WP1_PARK_RESOURCE_ONLY=1`はシェーダー・描画パイプライン共有、includeの内容変更、日本語パス、実行中の生成・削除・文字変更の資源再利用と失敗時の保持、描画補間・親子追従・直接移動・固定更新上限を検証します。UIのGPU退役と再読み込み失敗時の表示保持は`WP1_SCENE_UI_ONLY=1`、重力・速度変更時のJolt個体保持は`WP1_RIGID_ONLY=1`で確認します。


`WP1_PARK_UPDATE_ONLY=1`は通常更新を30回温めた後に240回測定し、`generated/park/updates.csv`へ処理別のCPU時間・呼出回数を保存します。描画とCSV出力はTotal update区間に含みません。Park検証では`LayoutDeltaValidation`により、全検証と差分検証、全世界行列解決と差分解決について正常・不正更新の判定一致と失敗時の出力保持を確認します。`WP1_RIGID_ONLY=1`はActorへの書込み途中の物理失敗で全Actorが元に戻ること、`WP1_SCRIPT_ONLY=1`は候補更新と従来の独立更新の結果一致も検証します。

## 配布Appの負荷計測

`App.exe --profile-park --profile-frames=600 --profile-output=C:/temp/park.csv` は配布Appの本番ループを計測します。タイトルは `--profile-title` を使います。入力フォーカスを取得できない検証環境では `--profile-simulate-inactive` を明示追加し、CSVの `input.active` と `simulation.override` を確認します。背景での物理・スクリプト・音声更新だけ継続し、実入力は非アクティブのままです。`Present wait` と `Frame fence wait` を処理時間から区別し、初期化直後のフレームを除外して比較します。詳細は `docs/ParkAdventure.md` に記録します。

`WP1_AUDIO_FEATURES_ONLY=1` の音声回帰は、設定未変更時の音源再構築省略、確定World行列と単体座標解決の出力一致、親移動への追従、ロード失敗時の再生保持、音量バインディング・awake・cueの変更、停止中の変更と再開、音源の生成・削除を確認します。
2026-10-13の追加検証: `WP1_SCENE_UI_ONLY=1` は文字再表示・色変更・整数ラスタ寸法・候補失敗時の履歴保持・32種類/16MiB上限をGPU描画と生成回数で確認します。`WP1_INPUT_ONLY=1` は診断Keyboardの押下/保持/解放とフォーカス復帰抑止も確認します。`WP1_PARK_RESOURCE_ONLY=1` は日本語の入れ子includeと別の配置先・作業フォルダーでも同じシェーダーrevision/バイトコードになることを確認します。時間の上限を合否条件にはしません。

配布Appの `--profile-replay-input` は決定的な診断入力です。実キー・マウスの計測ではこのオプションと `--profile-simulate-inactive` を外し、`input.device.active=1`、`input.diagnostic.replay=0`、`simulation.override=0` と実際の押下・座標・UI状態を確認します。診断再生のフォーカス検証では、`replay.update.index` で210～229、230、231を結合します。シェーダー事前生成はpackage.jsonのハッシュ記録前に実行されます。

## 外部ゲームプロジェクト

`powershell -NoProfile -ExecutionPolicy Bypass -File tests/TestGameProjects.ps1` は、空白を含むパスでプロジェクトを作成し、参照先・既存フォルダーの保護・テンプレート検証・元Contentの保持を確認します。
`-Build` を追加すると、ゲーム専用ソリューションのDebug／Development／Releaseビルド、ゲーム側素材と設定・DLLの配置、起動シーン検証まで実行します。

## 根本設計の移行契約

`tests/TestArchitectureContracts.ps1 -Configuration Debug -Baseline` は段階0の基準を記録します。
未達の契約は `KNOWN GAP` と表示され、移行完了とは扱いません。`-Require serialization-independent` などで
段階ごとの必須契約を指定でき、`-Baseline` を省略するとすべて必須になります。
Debug／Development／Releaseに対応し、CPUだけで検証します。元の制作データとユーザー設定は変更しません。
契約・移行順序は [ArchitectureMigration](../docs/ArchitectureMigration.md) を参照してください。


段階2の設計契約は、全36コンポーネントのコピー・比較・保存往復、個体参照の種類と複製経路、アセット改名とPrefab基準データ、Project／Materialの改名追跡、DLLなしのスキーマ読込と更新失敗時の保持を含みます。`ComponentContractValidation.h`、`AssetReferenceContractValidation.h`、`SchemaContractValidation.h` が検証の正本です。スキーマと実行コールバックを分離したSDKのABIは10で、実DLLの一致・不一致は `TestNativeBuildIdentity.ps1` で確認します。


段階2の利用側の確認は、共通ライブラリ・Editor・検証実行ファイル・対象プレイヤーをビルドした後に実行します。

```powershell
./tests/TestArchitectureIntegration.ps1 -Project <生成した最小ゲームのフォルダー> -AdditionalProjects Games/GuruttoWorld
# パッケージだけを確認する場合は -SkipRegression、既存回帰だけなら -Configurations @() を指定します。
```

ログは `generated/design-migration/integration-<ID>` に出力します。最小ゲーム・既存ゲームは3構成、Editorは対応する2構成を確認し、Debugの既存回帰5群を実行します。通し回帰全体の代わりにはしません。

### 純粋な保存と明示的移行

`MigrationContractValidation.h` は、DLL登録・解除前後のファイルとスナップショットの一致、未知の値の保持、構造の不足補完、配列の保持、移行の冪等性、複数オブジェクトの競合時の原状維持、上限超過、参照指定の変更、Undo／Redo、旧形式の正規化を検証します。

```powershell
./tests/TestArchitectureContracts.ps1 -Configuration Debug -Baseline -Require serialization-independent,migration-idempotent,migration-atomic-conflict
```

`serialization-independent` は既知の不足として許容しません。Prefabに関する後続段階の未達は引き続き基準モードで区別します。
既存のFollowupState／DeepStateの参照回帰は、保存に既定値の追加を頼らず、明示的な移行後の保存・改名追従を検証します。

統合ランナーの `-IncludeStateRegression` はAuthoring／FollowupState／SettingsCauseを追加します。`-SkipGpu` 時はGPUを必要としないAuthoringだけを追加します。一時ファイルは検証ごとにgenerated/tests/tempへ分離し、呼出元のTEMP／TMPを復元します。

### Prefabの接続範囲と配列の統合

`PrefabDesignContractValidation.h` は、未接続での配置・明示接続・保存と再読込・再抽出・旧形式の移行・Nested／Variant・識別子の領域の衝突・適用範囲・配列と格子の変更・競合時の原状維持を検証します。ユーザーデータのid／kind／typeと保存形式の型タグも区別します。

```powershell
./tests/TestArchitectureContracts.ps1 -Configuration Debug -Baseline -Require prefab-independent-elements,prefab-external-binding,prefab-slot-and-object-namespaces
```

`PrefabGpuContractValidation.h` は `WP1_PREFAB_RUNTIME_ONLY` で実行できます。統合ランナーはGPUを省略しない場合にこの群を含めます。旧形式を修復するための編集読込、通常実行での拒否、修復後の適用、欠損Prefabの失敗保持、既存PrefabPlaygroundを実際のGPUで確認します。
旧回帰で外部のメニューカメラを維持するケースも、接続先の個体を用意して明示的に接続します。Hierarchy／Scriptによる同一ドキュメント内の複製は従来の参照維持を検証します。

### 複数ファイルの編集と副作用予約

`EditTransactionContractValidation.h`は、準備段階の無書き込み、PrefabとメタデータのUndo／Redo、外部編集の拒否、部分書き込み・候補適用の失敗時の復元、復元中の外部変更保護、ID継承、履歴の分岐と上限、スクリプト音声予約の失敗破棄を確認します。既知の未達として許容する契約はなく、既定の厳格実行で確認します。
`WP1_PREFAB_RUNTIME_ONLY`はGPU資源準備の失敗を注入し、素材とWorldの保持、音声予約の破棄・一度だけの確定・Pauseと無効時刻での破棄も確認します。SDK ABIは12です。

### ドキュメントと実行セッション

`SessionDesignContractValidation.h`はGPUなしの編集・修復・Undo、投影の単一所有、固定した素材層と設定ID、参照宣言変更の診断、欠損個体参照の追加ロード・生成、Scriptのライフサイクルと開始候補を確認します。
`WP1_PREFAB_RUNTIME_ONLY`は編集の欠損資源、Playの設定・開始・モデル・UI失敗、失敗したStepの時計保持、Stop時のデータ維持と資源解放、参照宣言変更時の混在防止も確認します。ABIは13です。

### ゲーム拡張と旧経路の撤去

`ExtensibilityContractValidation.h`は宣言の継承・削除・往復、別ファイルの基底、循環・欠損・深さ・用途の競合、実行Registryからの独立性、新規ソースの正本と既存ファイルの保持を確認します。通常の設計契約に含まれます。

```powershell
./tests/TestScriptSchemaGeneration.ps1 -Configuration Debug -BuildNative
```

この生成検証は日本語のパス、大きなUTF-8既定値、決定的生成、不正宣言を拒否したときの生成物保持、生成C++の実DLL化と宣言の照合を確認します。Scriptという動作名でも、元のScriptSchema型が利用できることをコンパイルで確認します。`ScriptSchemaModuleValidation`は`--export DLL snapshot.json`、`--compare DLL snapshot.json [project]`、`--validate DLL project`でDLLのメタデータを実行コールバックなしで照合します。

統合ランナーはGPU有効時に`WP1_OPTIONAL_FEATURES_ONLY`と`WP1_DOCUMENT_SESSION_ONLY`を含めます。前者は空・粒子・UIのシェーダーがないゲーム、機能を追加したときの準備、準備失敗時のシーン保持と環境描画を確認します。後者はPlay／Stopで編集データ・選択・履歴の分岐・未保存の新規シーンが変わらないことと、Prefabの準備失敗時のファイル保持を確認します。

`WP1_MIRRORED_MESH_ONLY=1`は通し回帰の最後と同じ反転メッシュの描画を単独で確認します。通常・X鏡映・XY鏡映の面が見え、裏向きの面が除去されることをGPUの遮蔽サンプルで検査します。独自のコマンドリストにもRenderFrameContextを記録し、定数バッファーをGPUフェンス完了まで保持します。

`WP1_ALL_SCENES_ONLY=1`は公園用の静的登録を使用せず、実Appと同じGuruttoWorldのゲームDLLを読み込みます。実行する構成の`BuildScripts.ps1 -Content Games/GuruttoWorld -Configuration Debug`などを先に通してください。全シーンの保存往復・スクリプト開始・各フレームの更新・音声・描画と診断を確認します。`RunAuthoringIsolation.ps1`もTEMP／TMPを結果フォルダー内へ隔離し、呼出元の値を復元します。


### シーン・Prefabの層境界と更新費用

`TestArchitectureStructure.ps1` は3構成で `SceneDataValidation` も実行します。Engine / SceneRuntime をリンクせず、実際のシーン・Prefab計35ファイル、直接正規化、入れ子素材の再利用、上書き、循環・読取・検証失敗時の原状維持と再試行を確認します。`TestSceneLayers.ps1` はデータ層の推移的なinclude、プロジェクト参照方向、ソースの単独所有を検査します。標準ファイル入出力、素材索引、Windows、描画・物理ホストがデータ層に再混入すると失敗します。

GPU構造テストには、FixedUpdate / Update / LateUpdate の同一候補、外部準備からの中間状態の不可視性、準備回数、再入拒否、Runtime / UI 失敗からの再試行を含めています。

```powershell
./tests/TestSceneLayers.ps1
./tests/TestArchitectureStructure.ps1
./tests/TestScenePerformance.ps1 -Runs 5
```

性能ランナーはReleaseで、通常配置0 / 1,000 / 5,000個、100剛体、親子階層・Rotator・Scriptを含む1,000個を測定します。Prefabは1 / 8 / 32個体の入れ子素材について更新時間と依存素材の読取回数を記録します。初期化とGPU描画は計測に含めません。結果は `generated/architecture-fixes/performance-*` のCSVで、時間による合否判定は設けません。各依存素材を更新処理ごとに一度だけ読むことは回帰テストとして検査します。

`TransformResolutionCacheValidation.vcxproj` は座標の再利用結果を、キャッシュを使わない完全計算と比較します。親子・SRT・並び順の変更、重複ID・循環・欠損親・ゼロ拡縮・NaN、保持数を超える入力、返却結果の独立性を確認します。`PipelineReuseValidation.vcxproj` はシーン所有者の破棄後のPSO再利用、シェーダー改訂時の再生成、キャッシュ解放と現存所有者の有効性を確認します。どちらも3構成をサポートし、同じ確認を通常回帰にも含めています。

`RunRegression.ps1` の進捗には子テスト本体の累積CPU秒を併記します。経過秒との差にはGPU・ファイル・子プロセスの待機やスケジューリングが含まれ、CPU秒だけで原因を断定しません。`WP1_UPDATE_TIMING=1` を指定すると、設定画面と再生比較の更新CPU区間を描画時のリセット前に集計します。`WP1_INITIALIZATION_TIMING=1` は設定画面の初期化内訳を記録します。詳細計測のためにCPU計測値を消費するので、通常の所要時間比較はこれらを解除し、ビルドと他の回帰実行を重ねず、同じ構成を複数回完走させて範囲を記録してください。

`./tests/RunRegression.ps1 -Configuration Debug -Runs 3` は全回帰を3回直列に実行し、各回の秒数と最小・中央値・最大値を `generated/tests/regression-runs/repeated-*.json` に保存します。各回の失敗やタイムアウトはその場で失敗させ、成功した回だけで集計しません。既定の `-Runs 1` は従来と同じ単発実行です。
### 素材更新・計測・入力検証の追加回帰

`AuditCoreValidation` は、日時とサイズを保持した素材の編集・置換、依存一覧の再取得、骨格プレビューの更新と一時失敗後の再試行、CPU計測範囲、DLL名の検証をGPUなしで確認します。これらは通し回帰の `EngineEditorReviewValidation::Data` にも含まれます。

`TextureImportCacheValidation` はGPUを使い、画像・メタデータの日時を保持した変更、壊れたメタデータでの旧資源の拒否、正常化後の復旧も確認します。

```powershell
msbuild tests/AuditCoreValidation.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:PreferredToolArchitecture=x64 /m:1 /nr:false
./generated/tests/outputs/x64/Debug/AuditCoreValidation/AuditCoreValidation.exe
python tests/TestSceneMigration.py
./tests/TestRegressionRunner.ps1
```

`TestSceneMigration.py` は通常変換と再変換、複数入力の後続ファイルに非有限数・UTF-8化できない文字・不正階層がある場合の事前拒否、書込・同期失敗時の元ファイル保持と一時ファイルの削除を確認します。検証用データは `generated/tests/scene-migration` に作成し、制作シーンを変換しません。地域設定による小数点表記の違い、繰り返し実行と失敗時の打ち切り、先頭の失敗原因と末尾の診断表示は `TestRegressionRunner.ps1` が確認します。

`FileSaveRecoveryValidation` の失敗注入は、検証EXEのAPIインポートを一時的に置換して行います。静的ライブラリーも同じAPIを経由するため、Releaseの最適化でも停止・拒否の注入位置を保持します。製品側の保存実装はそのまま使用します。

