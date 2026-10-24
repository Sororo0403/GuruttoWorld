# 構造監査後の修正（2026-10-23）

対象は ArchitectureReview-2026-10-23.md の指摘。既存のゲーム制作データと、別作業の未コミット変更は保持した。
互換入口を残しながら、取得・所有・確定の境界を整理した。

## 取得したシーンと保存比較基準

SceneDocument::PrepareOpen は一度のファイル取得から、元のバイト列・解析済み配置・パスを含む候補を作る。
Apply はその配置だけを SceneWorld::ReloadLayout へ渡し、投影に成功した後、パスと保存比較基準を例外のない swap で受理する。
初回表示・再読み込み・通常の開く操作は同じ経路を通る。確定後のファイル再取得を廃止した。
取得後の外部変更は次の保存で競合として検出する。

## 編集投影と実行状態

SceneWorld は互換入口として残したが、Script・物理・入力・固定時間・補間姿勢の所有を SceneExecutionState へ分離した。
編集投影はこの実行状態を生成しない。編集用に空の物理ワールドを保持することもなくした。
実行用 World だけが SceneExecutionState を所有し、描画資源と実行状態の寿命を区別する。

フレームの復元対象は SceneExecutionState::Values にまとめ、FrameTransaction が値集合と物理保存点を所有する。
途中失敗・例外では RAII により復元し、成功時にだけ物理と開始・終了予約を確定する。
今後 Values に加える状態は、保存・復元の別々の列挙へ手で追加する必要がない。
描画資源の保持と補間キャッシュの無効化は同じ FrameTransaction が扱う。

## Script 定義と SDK

World の所有者設定時に、実行セッションが使う不変の ScriptDefinitions を固定する。
登録側の ScriptRegistry は既存ツール向けの入口として残すが、開始後の検証・更新段階の判断・コールバック・終了処理はセッションの集合を使う。
登録を交換・解除しても、既存セッションの定義と DLL の寿命は維持される。
所有者設定なしで ScriptRuntime を直接使う互換経路は登録側を参照する。独立した低レベル呼び出しでは、コンストラクターへ ScriptRegistry::Capture() の結果を渡す。

ScriptApi.h に DLL 向けの型と操作を分離し、ScriptModuleApi.h は ScriptRuntime の実装定義を取り込まなくなった。
物理の値型を PhysicsTypes.h へ分け、物理ワールドへ委譲する Raycast の実装は .cpp へ移した。
実 DLL の fixture は、公開入口から ScriptRuntime / PhysicsWorld が完全型として見えないこともコンパイル時に検査する。
生成 Schema ヘッダーも ScriptApi.h を利用する。

## 資源変更と GPU 同期

描画資源の変更規則を SceneResourceChanges に集約し、Editor はその判定へ委譲する。
メッシュ・アニメーション資源の再準備も同じ規則を使う。
UI は安全側に内容・構成全体を比較するため、以前より待機対象が増える場合がある。

シーン交換では SynchronizeGpuQueue を使い、フェンスで先行コマンドの完了を待つ。
同期のためのダミーテクスチャ生成を廃止した。

## シーン操作の成功単位

World の更新成功と、予約したシーン操作の成功を別の結果として定義した。
シーン操作は予約順に一件ずつ確定し、一件の失敗で先行して成功した操作を取り消さない。
ホストは SceneEnvironment::SceneCommandResults() から、操作・成否・診断を取得できる。

Script には次の更新で sceneCommandCompleted / sceneCommandFailed を通知する。
sender はシーン名、value は通常ロード 0・追加ロード 1・アンロード -1、text は診断（最大4096バイト）を表す。
GamePlayback::LastUpdateSucceeded は World 更新と副作用予約の確定を表し、シーン操作の全成功とは解釈しない。

## DLL ビルドと公開

- 出力先・構成ごとにビルド全体を排他し、待機した古いビルドが新しい入力の成果物を後から上書きすることを防ぐ。
- 共有 SDK の構築・Schema 生成・DLL コンパイルも、同じリポジトリ・構成では共有セッションで保護する。
- manifest は固有の一時ファイルへ同期してから原子的に置換する。失敗時は前の manifest を保持する。
- プロセス終了による放棄されたセッションから復帰できる。
- BuildScripts はファイルの存在だけを信頼せず、依存ライブラリーをビルドしてから DLL をコンパイルする。
- Script・SDK ソース・リンクするライブラリーを前後比較し、途中で変わった候補は公開しない。
- manifest に実際の Engine.lib / SceneRuntime.lib の SHA-256 を記録する。

EngineBuildInputs はエンジン識別と SDK 入力の列挙を共通化する。
外部ヘッダーと依存構築スクリプトも識別対象に含める。
取得した Jolt / Assimp ソースは DLL 構築中の変更検知と、依存ライブラリーの増分ビルド判定へ含める。
初回取得の有無で識別が循環しないよう、エンジンのソース識別と取得済み依存の監視を区別する。

## CPU データ層

```text
ScriptSchemaCompiler ──→ SceneData
SceneRuntime ──────────→ SceneData / Engine
Editor / App ──────────→ SceneRuntime / Engine
```

SceneData は Schema の解析・保存と ScriptValue の codec を持ち、Engine / SceneRuntime のライブラリー、Jolt、描画実装を参照しない。
Engine の JSON / 値検証ヘッダーは利用する。
コンポーネントへ適用する移行処理は ScriptSchemaMigration.cpp として Runtime 側へ分けた。
Runtime のライブラリーには SceneData を組み込み、既存の DLL リンク入口を維持する。
プロジェクト生成と既存のソリューションにも SceneData を登録した。

## 検証

再実行の入口:

```powershell
./tests/TestArchitectureStructure.ps1
```

Debug / Development / Release の新しい構造テスト、既存の設計契約、GPU の監査回帰、実 DLL の識別、独立した Schema 生成を実行する。
Editor は対応する Debug / Development、既存ゲームは3構成をビルドする。
Editor のプロジェクト検証とゲームのパッケージ検証も含む。
-SkipGpu は GPU を使う実行を省略し、-SkipHosts は Editor / ゲームの構築・実行を省略する。
結果と標準出力・エラー・ビルドログは generated/architecture-fixes/validation-* に保存する。

追加した契約:

- 取得後の外部編集、BOM、取得後のパスの読み取り不能、解析失敗、投影拒否で、正本と保存基準を取り違えない。
- 編集投影は実行更新・物理問い合わせを開始しない。
- Transform だけの変更と、文字描画・マテリアル資源の交換を区別する。
- 同名 Script 定義の交換・解除後も、独立したセッションが元の動作を維持する。
- 有効・無効なシーン操作を同じ更新で予約し、順序付き結果と次の Script 通知を確認する。
- 明示的な GPU 同期が先行フェンスの完了を保証する。
- 別プロセスの公開排他、manifest 置換失敗時の旧版保持、一時ファイル回収、プロセス強制終了後の復帰を確認する。
- SDK 入力の列挙が外部ヘッダーと依存固定スクリプトを含み、出力物を循環入力にしない。

最終差分での結果:

| 確認 | 結果 |
|---|---|
| ArchitectureStructureValidation | 3構成で成功。GPU同期・通知を含む |
| ArchitectureContractValidation | 3構成で148契約、失敗0 |
| ArchitectureAuditFixValidation | 3構成で成功。既存の失敗注入・再試行・GPU寿命を含む |
| 実DLLの識別と公開SDK境界 | 3構成で149契約、失敗0。不一致は登録前に拒否 |
| 独立したSchema生成 | 3構成で成功。失敗時保持・継承・決定性・UTF-8 |
| Editorビルドとプロジェクト検証 | Debug / Developmentで成功 |
| GuruttoWorldビルドとパッケージ起動 | 3構成で成功 |
| GamePlaybackFocusedValidation | Developmentで成功。実App/Editorのフレーム一致、CONFIG、START/QUIT、Pause/Step/再開 |
| DLL公開のプロセス間検証 | 成功。Windows PowerShellとPowerShellから実行 |
| SDK入力列挙・プロジェクト生成/移動 | 成功 |
| Lizard | 2,371関数、CCN > 15 / 長さ > 100 / 引数 > 8 の超過0 |
| FormatCode.ps1 | 26ファイルを確認、24ファイル成功。既存2ヘッダーの未整形は維持 |
| git diff --check | 成功 |

既存の未整形は SceneEnvironment.h と ScriptModuleApi.h。作業開始時のコピーでも同じ整形違反になることを確認し、一括整形へ巻き込まなかった。
追加したコードと他の変更ファイルに整形違反はない。

ログは generated/architecture-fixes/validation-d587acc2bd6d4c9da5fc6d3f26882a65、Releaseの実DLL再検証は native-Release-final.log と schema-Release-final.log。
途中のRelease実DLL検証では、SDK更新後に古い検証ホストを使用した不一致が発生した。
TestNativeBuildIdentity.ps1 がDLL構築後に照合ホストも構築するよう修正し、同じSDKで再検証して成功した。
過去の失敗ログは成功結果に読み替えず、経緯の確認用に保持した。

## 更新費用の基準計測

`tests/SceneExecutionProfile.vcxproj` を Release でビルドし、生成した SceneExecutionProfile.exe を実行するとCSVを出力する。
Scriptと描画モデルを持たない個体、または重力なしの100剛体について、3フレームの準備後に20フレームを測定した。
GPU描画時間は含めない。同一マシンでの単発値であり、時間によるテスト合否判定は設けていない。

| 個体数 | 物理 | World更新平均ms | Components平均ms | 物理更新平均ms | 外側の残差ms |
|---|---|---:|---:|---:|---:|
| 0 | なし | 0.003930 | 0.002420 | 0 | 0.001510 |
| 1,000 | なし | 3.677795 | 1.913975 | 0 | 1.763820 |
| 5,000 | なし | 24.059810 | 13.480605 | 0 | 10.579205 |
| 100 | あり | 1.058505 | 0.062585 | 0.643190 | 0.352730 |

残差は保存点・確定・計測などを含み、割り当て量やコピー時間そのものを分離した値ではない。
結果は generated/architecture-fixes/profile.csv。5,000個体では更新だけで約24msを要するため、大規模シーンの全体コピーと候補準備の削減は残る課題として扱う。
正しさの修正だけで性能問題も解消したとは判断しない。

## 第1段階終了時の残る改善と保証範囲

- SceneWorld の互換 API と Editor の画面統括は維持している。すべての操作 API を別クラスへ移す変更は今回行っていない。
- CPU ライブラリー分離は Schema / ScriptValue が対象。シーン・Prefab・アセット操作全体の独立ターゲット化は残る。
- フレームの全体コピーと物理保存点の費用は残る。上記の基準計測を残した段階であり、大規模シーンの性能改善を実証したものではない。
- Windows / MSVC と同じ構成を使う C++ DLL の前提は維持する。任意のコンパイラーや成果物の差し替えまで保証する安定 C ABI にはしていない。
- 名前付きビルドセッションは同じ Windows セッション内を対象にする。協調しない外部ビルドによる更新は前後の入力比較で拒否する。
- 新しい失敗注入・GPU 回帰と実 DLL の検証は、全ゲーム操作の通しプレイや電源断耐久性の再検証を置き換えるものではない。

監査の全項目を無条件に「解消済み」とは扱わない。正しさと所有権の修正を先に適用し、残る分割・計測は上記の単位で継続できる状態にした。


## 第2段階: シーン・Prefab全体の層分離と候補所有権の整理

以下は前節の残項目に対する継続修正。第1段階の計測・検証記録は当時の結果として保持する。

### データ・ストレージ・実行投影

- `SceneData.vcxproj`: SceneLayoutの解析・検証・正規化・保存形式、全36コンポーネントのJSON、Prefabの抽出・配置・上書き・Variant・参照継承・再帰マージ、階層変換、Animationの値処理、Animator / Ragdollの保存設定検証。Engine / SceneRuntimeへのプロジェクト参照を持たない。EngineのJSON・数値検証・不変データ型とDirectXMathのCPU計算は使用する。
- `SceneStorage.vcxproj`: シーンのファイル取得・保存、アセット索引による参照解決、Prefab素材の取得・保存。SceneDataとEngineだけを参照する。SceneWorldや物理・描画ホストを参照しない。
- `SceneRuntime.vcxproj`: 描画資源の投影、物理、Scriptの実行、Animatorのポーズ評価、フレーム取引。データ操作を再実装しない。互換性のため静的ライブラリーにはSceneData / SceneStorageのオブジェクトも含め、既存のゲームDLLのリンク方法を維持する。
- ScriptSchemaのファイル探索も純粋な解釈から分離した。単独SchemaCompilerは `ScriptSchemaFiles` という標準ライブラリーだけの入出力アダプターを使い、引き続きSceneDataだけをリンクする。既存のLoadProject APIはストレージ側の委譲として残す。

組込Scriptの宣言は実行コールバックから生成せず、SceneData側の不変な正本へ移した。実行定義は同じ宣言を参照してコールバックだけを追加する。データ層だけをリンクした検証からも全6宣言を取得できる。

MaterialAssetの保存規則を `MaterialAssetFormat`、論理アセットパスの規則を `AssetPath`、UI状態と式を `UiState` に分離した。同じ検証をコピーして別々に保守する方式にはしていない。Runtime側の既存APIも共通の規則へ委譲する。

`TestSceneLayers.ps1` はデータ層からたどれる70ソース・ヘッダーを検査する。標準ファイル入出力、AssetDatabase / ContentContext、Windows、描画・物理・実行ホストの逆依存を拒否する。`SceneDataValidation` はEngine / SceneRuntimeをリンクせずに実行し、公開ヘッダーからホスト型やWindows / D3D12が露出しないこともコンパイルで検査する。

### 更新の候補を一つにする

以前はフレーム保存点に配置・描画資源一式をコピーし、固定更新の候補を確定してから、通常更新で再び候補をコピーしていた。失敗時には中間確定した配置・資源を再配置する必要があった。

現在は `ExecuteUpdate` が一つのSceneLayout候補を所有し、固定更新→通常更新→後段更新→Runtime / UIの準備・検証へ引き継ぐ。成功したときだけ配置と描画投影を一度確定する。保存点は実行値と物理に限定し、物理取引も一つに統合した。MovePlayers / UpdateComponentsの単独呼出も同じ取引を使う。

通常更新では全配置のコピーを2回から1回へ、固定更新を含むフレームでは3回から1回へ減らした。描画資源ベクターと実行値の重複保存も除いた。LateUpdate・RootMotionなどが必要とする候補や、変更した描画資源の準備は残す。可変参照を返す既存APIで無条件の共有コピーを導入すると保存点が書き換わり得るため、その方式は採用していない。

外部の準備コールバックは完成した候補を受け取り、Worldの確定済み配置には途中の固定更新を公開しない。接触通知の宛先判定も、グローバル登録表ではなくセッションのScript定義を使うよう統一した。

### Prefab依存の一回解決と不要な文字列往復

`Prefab::RefreshPrepared` は読み取り関数を明示的に受け取る。更新処理ごとのresolverが解決済み素材を所有し、同じPrefabを複数個体や複数の入れ子から参照しても、読取・再帰解決は一度だけ行う。失敗したresolverは破棄し、キャッシュも更新候補も外部へ公開しない。循環と64段の上限を維持する。永続キャッシュではないため、次回更新で素材の変更を取りこぼさない。

ストレージ側は必要になった時点でアセット索引を一度だけ取得し、読み取り済みの未保存素材候補を優先する。全体の検証後にシーンを置き換え、外部接続口・識別子・配列マージの既存契約を維持する。

読取専用のOverridesとEditOverrideは全シーンの一時コピーを作らない。SceneLayout::Normalizedは保存形式のDOMを直接正規化し、参照解決時のSerialize→文字列Parseの往復を除いた。実ファイル35件について、従来の保存形式を経由した結果と一致することを検査した。直接DOMを扱ってもNaN・無限大・不正なUTF-8の値とキーを受理しない。UIと正規化は共通のUTF-8検査を使用し、過長表現・サロゲート・Unicode範囲外・途中切断の境界ケースを検査する。マテリアルの汎用シェーダー値も差分検証で非有限値を拒否する。

### 再現コマンド

```powershell
./tests/TestArchitectureStructure.ps1
./tests/TestScenePerformance.ps1 -Runs 5
```

候補の複製をフレーム全体の所有者へ移したため、Componentsや外側の残差の計測区間は第1段階と異なる。前後比較にはWorld更新全体の経過時間を使う。個別のコピー時間・割り当て量・GPU描画時間を測った結果ではない。

性能・3構成の最終結果は、検証完了後に次節へ記録する。
