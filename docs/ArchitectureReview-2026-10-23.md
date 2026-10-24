# 構造・依存関係の監査（2026-10-23）

本書は修正前の監査記録。対応内容と検証・残る範囲は [修正記録](ArchitectureFixes-2026-10-23.md) を参照する。

## 結論

プロジェクト間の依存方向は妥当で、全体が根本から破綻しているという証拠はない。
ただし、責務と成功境界の集中、SDK と内部実装の結合、ビルド公開の競合には改善の必要がある。
フォルダーの再編や全面書き換えから始めず、読み込み時の整合性、公開の排他、所有権と確定境界の順に対処する。

以下は修正済み一覧ではなく、現時点の調査結果と修正・追加検証の候補である。
「実行確認」「コード確認」「設計上の制約」「要追加検証」を区別する。
P1 はデータ整合性を優先調査するもの、P2 は構造改善・運用の安定化、P3 は保守性改善を表す。
実行時の不具合を再現していない項目を、再現済みの不具合として扱わない。

## 対象と方法

- 現在の作業ツリーを対象とした。既存の未コミット変更と未追跡ソースも含むため、HEAD だけの評価ではない。
- CONTRIBUTING.md、設計移行記録、テスト手順、主要なプロジェクト設定、主要な読み込み・編集・実行・描画・保存・DLL 経路を確認した。
- Engine、SceneRuntime、Editor、App の自作 C++ 318 ファイルについて、明示 include のグラフを作成した。
- 318 ファイルすべてを同じ深さで目視精読したという意味ではない。主要経路を精読し、全体集計と既存テストで補った。
- 外部ライブラリー内部、Windows SDK 内部、シェーダー全体、全ゲームスクリプトの仕様、全 GPU 経路を網羅する監査ではない。
- 実装は変更していない。監査用の集計・検証出力は generated 以下に置いた。

集計の再実行:

```powershell
python generated/analysis/architecture-review-20261023/audit_dependencies.py
```

出力は同ディレクトリの dependencies.json。対象ファイルの SHA-256 も含む。
監査スクリプトと出力は generated 配下のため Git 管理対象ではない。
これはプリプロセッサーの完全な解析ではなく、外部ヘッダー・マクロによる include は追跡せず、条件付き include は合算する。

## 実際の依存構造

矢印は依存先を表す。

```text
Editor ──────→ SceneRuntime ──→ Engine
App ─────────→ SceneRuntime
Editor / App ─────────────────→ Engine
GuruttoWorld.vcxproj ──import──→ App.vcxproj
ゲーム DLL ──link─────────────→ SceneRuntime.lib / Engine.lib
```

ゲームの実行ファイルは共通 App のプロジェクトを import する。
ゲーム処理は Assets/Scripts 以下から別 DLL に生成される。
エンジンと SceneRuntime は静的ライブラリーである。

| 集計 | 結果 |
|---|---:|
| Engine の自作 .h / .cpp | 113 |
| SceneRuntime の自作 .h / .cpp | 104 |
| Editor の自作 .h / .cpp | 89 |
| App の自作 .h / .cpp | 12 |
| 解決できた自作 include の循環 | 0 |
| ScriptModuleApi.h から到達する自作ファイル | 34 |
| SceneLayout.h から到達する自作ファイル | 22 |
| SceneDocument.h から到達する自作ファイル | 67 |

循環がないことは責務分離の十分さを意味しない。今回の主要な問題は逆依存より、公開境界が広いことにある。
Engine から SceneRuntime / Editor / App への直接 include は確認されなかった。
Engine、SceneRuntime、Editor の src 以下の .cpp にプロジェクト未登録は見つからなかった。
App は WP1Root を使った登録であり、変数を展開しない単純比較では未登録判定できない。実際の登録を別途確認した。

## 指摘一覧

| ID | 優先度 | 分類 | 内容 |
|---|---|---|---|
| A01 | P1 | コード確認・競合再現は未実施 | シーン読み込み内容と外部変更比較基準が別の読み取り |
| A02 | P2 | コード確認・同時実行再現は未実施 | DLL manifest 公開の一時ファイルと出力先が共有 |
| A03 | P2 | 設計上の制約 | SceneWorld が編集投影と実行 World を兼ねる |
| A04 | P2 | 設計上の制約 | GPU 同期の要否を Editor が個別列挙で判断 |
| A05 | P2 | コード確認・性能影響は未計測 | フレーム取引の保存点とコピーが複数層に分散 |
| A06 | P2 | 仕様確認が必要 | 更新成功とシーンコマンド成功が異なる境界 |
| A07 | P2 | コード確認 | GPU 同期をダミーテクスチャの生成に依存 |
| A08 | P2 | 設計上の制約 | DLL SDK が実行実装・シーン全型へ依存 |
| A09 | P2 | 運用条件・追加検証が必要 | 単独 DLL ビルドがライブラリーの最新性を確認しない |
| A10 | P2 | 設計上の制約 | ScriptRegistry が実行セッションでなくプロセスの共有状態 |
| A11 | P2 | 設計上の制約 | CPU データ契約も描画・物理を含む共通ビルドへ依存 |
| A12 | P3 | コード確認・運用改善 | 外部依存の増分判定とビルド識別子の保証範囲が限定的 |
| A13 | P3 | 保守性 | Editor の統括処理・大きな公開ヘッダーに変更が集中 |

### A01: シーン読み込みと保存基準を同じ取得結果から作っていない

根拠: Editor/src/Documents/SceneDocument.h の Apply、SceneRuntime/src/Scene/SceneWorld.cpp の Reload、SceneRuntime/src/Scene/SceneLayout.cpp の Load。

Apply は world.Reload を実行し、World が切り替わった後で path_ と unsavedNew_ を更新し、ReadSource(path_) で savedSource_ を取得する。
Load が解析したファイル内容そのものを保存基準として受け取っていない。

成立し得る順序:

1. Reload がファイル内容 A を読み込む。
2. 外部アプリが同じパスを内容 B に変更する。
3. Apply の ReadSource が B を保存基準として記録する。
4. ユーザーは A を基に編集し、通常保存の外部変更チェックは B と現ファイル B の一致で通る。

この場合、ユーザーが読み込んでいない外部版 B を競合として検出できない。
また、最後の ReadSource が例外になると false を返す一方、World と path_ は既に切り替わっている。
失敗時に元のドキュメントと World を保持するという Apply の説明と一致しない経路がある。

これはコード順序から確認した競合経路であり、今回タイミングを制御した実行再現はしていない。
ファイルの取得結果を「生データ・解析済み Layout・保存比較基準」の一つの候補として渡し、最後にまとめて交換する必要がある。
再読み取り・削除・読み取りエラーを読み込み後段へ注入し、World・パス・保存基準の組を比較する検証が必要。

### A02: DLL 公開先にプロセス間の確定境界がない

根拠: scripts/BuildScripts.ps1 の workspace / RunId と manifestPath.tmp の保存処理。

コンパイル作業領域と DLL 名は RunId で分離されているが、同じ OutputContent に対する module.json.tmp は共有される。
複数ビルドが重なると、一方が他方の一時ファイルを移動する、他方の Move-Item が失敗する、先に開始したビルドが後から最新 manifest を上書きする経路がある。
ソースの前後ハッシュは各ビルドの入力変更を検出するもので、公開同士の競合を解消しない。

通常の Editor 一画面で必ず発生するという意味ではない。複数 Editor、CLI と Editor、複数ビルドジョブの重複が条件。
同じ出力先への公開を排他し、一時ファイルにも固有名を使い、必要なら公開世代を照合する。
ゲーム本体の既存 DLL 出力を使わない fixture で、公開直前に二つの子プロセスを同期させる検証が必要。

### A03: SceneWorld の役割をコンストラクターと nullable な状態で切り替える

根拠: SceneWorld.h の二つのコンストラクター、IsDocumentView、scripts_ / physicsWorld_ / document_ / models_。

実行用は ScriptRuntime を所有し、編集用は共有 SceneDocumentData を受け取り、scripts_ の有無でモードを判断する。
同じ型に編集操作、実行更新、物理、モデル、描画、アニメーション、Script の取引が載っている。
SceneDocumentData も view_ と friend SceneWorld を持ち、投影中の変更を SceneWorld 経由に制約する。

ただし、描画なしの編集・修復・Undo は既に実装され、今回の契約テストでも成功した。
「データと描画が全く分離されていない」「描画なしの編集が不可能」という評価は誤り。
問題は、独立した機能が大きな一つの型のモードとして表現されていること。

編集操作を担当するサービス、実行状態を持つ RuntimeWorld、描画資源を持つ RenderProjection の境界を明示する。
既存の SceneDocumentData / ScenePresentation を活かし、公開互換入口を残して段階的に移す。
編集専用コードが ScriptRuntime や PhysicsWorld を所有しないことを型・ターゲット単位で検証する。

### A04: GPU 資源の知識が Editor と Runtime に重複する

根拠: Editor/src/Inspector/ComponentEditResources.h の Changed / NeedsIdle、SceneWorld.cpp の資源準備、Editor/src/main.cpp の待機判断。

Editor が terrain、tilemap、inputField、meshRenderer、material、animator などを個別に比較し、GPU 待機の要否を決める。
描画資源を実際に変更する側とは別に、同期のための依存表が存在する。
新しい資源やコンポーネントの追加時に両方を更新する必要がある。

現時点で列挙漏れによる GPU 不具合を再現したわけではない。
資源更新側が変更計画と必要な同期・退役処理を返す形へ寄せる。
ComponentDefinitions の正本があることと、全派生規則がそこから生成されることは別なので、移行時に混同しない。

### A05: 一つのフレームを成立させる取引が複数層にまたがる

根拠: SceneTransactions.cpp の UpdateFrame、SceneWorld.cpp の RuntimeLayout / UpdateComponents / PrepareCachedLayout、PhysicsWorld.cpp の Transaction、SceneEnvironment.cpp の UI 候補管理。

UpdateFrame は配置・描画用保持参照・Script 状態・物理状態・補間姿勢・固定時間などを保存する。
内側の UpdateComponents でも Layout と ScriptRuntime をコピーし、物理では SaveState を取得する。
UI 候補と音声コマンドの確定・破棄は SceneEnvironment に分かれる。

失敗時復元を実装していること自体は強みであり、契約テストもある。
懸念は、新しい状態を追加するたびに保存・復元・成功後確定の対象を複数箇所で揃える必要があることと、通常フレームでも保存点の費用を負うこと。
コピーの存在は確認したが、フレーム時間や割り当て量の悪化は今回計測していない。

まず FrameTransaction が所有する状態一覧と確定順を明文化する。
その後、1千・1万オブジェクト、Script 数、物理数を変え、保存点取得・候補更新・確定の時間と割り当て量を測る。
測定前に ECS 導入や全コピー撤去を決めない。

### A06: Update 成功は、予約したシーン操作の成功まで意味しない

根拠: SceneEnvironment.cpp の Update / ProcessSceneCommands、GamePlayback.h の Update / LastUpdateSucceeded。

World の更新確定後にシーンコマンドを取り出し、各 LoadScene / UnloadScene を順に実行する。
失敗は警告ログに記録し、後続コマンドへ進む。ProcessSceneCommands は結果を返さない。
その後 Update は true を返し得る。

そのため複数の予約コマンドはバッチとして原子的ではない。
これは必ずしも不具合ではないが、「フレーム成功」の意味を利用側が誤解しやすい。
全フレームを巻き戻す方針か、コマンド単位で結果を返す方針かを明示し、失敗通知を Script / ホストへ渡す。
有効ロード・無効ロード・アンロードを一つのフレームで予約した際の仕様を固定するテストが必要。

### A07: 同期待機をテクスチャ生成の副作用で行っている

根拠: SceneEnvironment.cpp の ApplySceneLayout にある fenceTexture.Initialize(..., {})、Texture2D.cpp の Initialize / UploadAndWait。

シーン交換前に空パスの Texture2D を作り、そのアップロード完了待機でキューの同期を成立させている。
同期のためにテクスチャ・アップロード用資源などの生成が必要になり、テクスチャ生成の失敗がシーン交換の失敗条件になる。
将来アップロードを非同期化すると、呼び出し側の意図が壊れる結合でもある。

既存の GPU 同期・退役 API を使った、目的の明確な同期へ置き換える候補。
現実装が同期をしていないという指摘ではなく、同期契約を別機能の副作用に依存している点の指摘。

### A08: DLL SDK の境界が広い

根拠: ScriptModuleApi.h、ScriptRuntime.h、SceneLayout.h、scripts/BuildScripts.ps1。

公開入口が実行実装のヘッダーを取り込み、ScenePlacement の全コンポーネント型と物理 API に依存する。
ScriptDefinition など C++ の型を DLL 境界で受け渡す。
現方式は同じ MSVC・ランタイム・構成・エンジンで揃える内部拡張としては成立するが、独立した安定 SDK ではない。

ABI 14、compiler、iteratorDebug、型サイズ、engineBuild の検査は存在する。
「ABI 検査がない」「DLL の寿命を保持していない」という指摘は当たらない。ModuleCallback が DLL の寿命を保持している。
まず SDK 宣言と実行実装を分け、利用する操作だけを公開する。
独立配布や複数コンパイラーを要求しない限り、全面的な C ABI 化を必須とはしない。

### A09: 単独 DLL ビルドの前提を機械的に保証していない

根拠: BuildScripts.ps1 の engineLibrary / sceneLibrary 存在チェック、ScriptModuleAbi の Engine::BuildIdentity 呼び出し。

BuildScripts は一致するライブラリーを先にビルドすることを要求するが、実際の検査は .lib の存在である。
現在のヘッダーと既存の古い .lib を組み合わせても、ビルド開始時点では拒否しない。
engineBuild はリンクした Engine.lib の関数から得るため、現在読んだヘッダーの識別子を独立に検査しているわけではない。

通常の App ビルドは参照ライブラリーを先に構築するので、この問題を軽減している。
問題条件は Engine / SceneRuntime 編集後に BuildScripts だけを直接実行するなどの運用。
同サイズのヘッダー変更を含む stale library の fixture を作り、どこまで拒否できるか確認する必要がある。
ビルド入力の識別子をライブラリー成果物に付け、SDK ヘッダー側の識別子と照合するか、単独入口から依存ビルドを保証する。

### A10: ScriptRegistry の所有範囲がプロセス全体

根拠: ScriptRuntime.cpp の Registry / RegistryRevision / InstallModule、ScriptRuntime.h の static API。

モジュール所有者ごとの登録管理と候補交換はあるが、名前空間はプロセス内で共有される。
各 Runtime が独立した Registry を受け取る構造ではなく、異なるプロジェクトの同名 behaviour は同時に独立登録できない。

一つの Editor で一つのプロジェクトを扱う現運用なら合理的な制約。
また ScriptAuthoringPanel は停止中にだけ再インストールするため、通常 UI が再生中に無条件で DLL を交換するという指摘は誤り。
複数プロジェクト同時編集・独立 World・テスト隔離を必要とする段階で、ProjectContext / 実行セッションが不変の Registry を所有する形へ移す。

### A11: データと検証を独立してビルドする境界がない

根拠: ArchitectureContractValidation.vcxproj の ProjectReference、SceneRuntime.vcxproj の構成と Jolt.targets。

CPU の保存・Schema・Prefab 契約でも SceneRuntime と Engine 全体へ依存する。
実行時に GPU デバイスを使わないテストであっても、描画・物理を含むライブラリーのビルド環境が必要。
今回は Debug の SceneRuntime.pdb を開けず、この依存のため CPU 契約のビルドも止まった。

PDB エラーの原因が設計にあると断定するものではない。
データ型・codec・参照変換・Schema をまとめた小さなターゲットを作ると、CPU 契約を実行機能から切り離せる。
プロジェクト数を増やすこと自体を目的にせず、コンパイル・リンク依存が減ることをゲートにする。

### A12: 外部依存と識別子の保証範囲

根拠: GenerateEngineIdentity.ps1、Engine/build/Assimp.targets、SceneRuntime/build/Jolt.targets、BuildAssimp.ps1、BuildJolt.ps1。

外部ライブラリーの取得時には具体的なコミットを検証しており、バージョン固定が全くないわけではない。
ただし通常の増分ビルドはスクリプト・targets と .ready の時刻、必要ファイルの存在を主に使う。
取得済みソースの内容変更や成果物の差し替え自体は、毎回の検証入力にはなっていない。

engineBuild のハッシュ対象も自作ソースと一部設定であり、外部ソース・実ライブラリー・全ツールチェーン・環境変数まで含む完全な成果物識別ではない。
「ソース変更検出」と「ABI 互換性」と「再現可能ビルド」を同じ保証として扱わない。
依存リビジョン・コンパイラー・構成・主なフラグを記録するビルド manifest と、キャッシュ再検証経路を設ける候補。

### A13: 統括処理と公開ヘッダーに保守上の変更が集中する

根拠: Editor/src/main.cpp、SceneWorld.h、AssetDatabase.h、FileJournal.h、MaterialAsset.h。

Editor の main.cpp は 2,066 行、SceneWorld.cpp は 2,766 行。
ただし関数単位の分割は既に進んでおり、今回の Lizard 集計では 2,355 関数に対し CCN > 15 / 行数 > 100 / 引数 > 8 の超過は 0 件だった。
したがって「巨大な関数が未整理のまま」という評価はしない。

問題は、分割した関数の多くが同じ状態・ライフサイクルを共有する統括領域に残ること。
また AssetDatabase など公開ヘッダーに Windows・ファイル処理・JSON 操作など実装詳細が含まれる。
前方宣言や .cpp 化で公開面を減らし、Editor のドキュメント操作・再生操作・保存・描画準備を所有者単位で切り出す。
関数を別ファイルへ移すだけでは、依存と所有権の改善にはならない。

## 問題として扱わなかったもの

- Editor と App が Engine を直接参照すること。ウィンドウ・入力・描画のアダプターを担当するため、直ちに層違反ではない。
- ScenePresentation / SceneEnvironment / SceneDocumentData が存在しないという仮説。すでに分離の基盤がある。
- 描画なしの編集ができないという仮説。今回実行して成功した。
- 保存が ScriptRegistry の状態で変わるという旧課題。現契約の migration-pure-save / serialization-independent は成功した。
- Prefab が偶然一致した外部 ID を捕まえるという旧課題。明示バインディングと契約がある。
- DLL 解放後のコールバックを無条件に実行するという仮説。ModuleCallback に寿命保持がある。
- 外部依存が未固定という仮説。取得スクリプトにコミット検査がある。
- ECS・DI コンテナー・動的コンポーネントを採用していないこと。それだけでは欠陥と判断しない。
- Editor が Release 構成を持たないこと。現在の開発用ツールの構成方針として扱う。

## 今回の検証結果

| 検証 | 結果 |
|---|---|
| 明示 include / ProjectReference 集計 | 318 ファイル、解決した include の循環 0 |
| Lizard による関数集計 | 2,355 関数、指定閾値の超過 0 |
| TestArchitectureContracts.ps1 -Configuration Release | 再ビルド・実行成功、148 契約、既知不足 0、失敗 0 |
| TestArchitectureContracts.ps1 -Configuration Debug | SceneRuntime.pdb を開けず C1041、契約実行に到達せず |
| TestProjectContext.ps1 | 成功。最小テンプレート、ID、移動、決定的生成、非互換記述、旧プロジェクト保護 |
| TestSharedAssetLayers.ps1 | 成功。共有層、上書き、ID、不変スナップショット、同時編集など |
| TestBuildContentSnapshot.ps1 | 成功。不変スナップショット、一時保存除外、同時編集拒否 |
| TestScriptSchemaGeneration.ps1 -Configuration Release | 成功。正本の解析、ファイル間継承、決定的生成、失敗時保持、UTF-8。実 DLL の BuildNative は未指定 |

契約のログは generated/design-migration/contracts-Release-results.log、ビルド失敗の詳細は contracts-Debug-build.log。
Release の成功を Debug / Development / GPU / 実 DLL 互換性の成功へ拡張して解釈しない。
今回の契約実行に --module は指定していないため、149 件目の実 DLL 契約は未実施。
Cppcheck の現時点の再実行、GPU を含む通し回帰、故障注入による A01 / A02 再現、性能測定も未実施。
過去の検証記録は参考とし、今回の検証結果とは分けた。

## 修正に進む順序と完了条件

1. A01 の読み込み候補を一体化する。外部変更・読み取り失敗を後段へ注入して、データ・パス・保存基準の一貫性を検証する。
2. A02 の DLL 公開を排他する。二つのビルドの公開競合、失敗時の旧 manifest 保持、DLL と manifest の対応を確認する。
3. A06 の成功境界を定義する。シーン操作の失敗をどこへ返すかを仕様化し、既存の音声・UI・時計の契約を維持する。
4. A03 / A04 / A07 を所有権の境界で整理する。編集と実行、資源変更と同期の責任を型として表現する。
5. A05 を計測し、結果に基づいて保存点や差分更新を改善する。復元保証を先に弱めない。
6. A08 / A09 / A11 で SDK とデータの依存を絞る。最小ゲームと CPU 契約を小さな依存集合でビルドできることを確認する。
7. A10 / A12 / A13 は必要な運用範囲を決めて段階的に進める。複数プロジェクト対応など要求していない機能を先回りして全面導入しない。

共通コードの修正時は CONTRIBUTING.md に従って Debug / Development / Release を確認し、変更 C++ だけを FormatCode.ps1 で検査する。
既存の未コミット変更、一括整形、機能修正、コメント翻訳を混在させない。
