# M0〜M7 の実装記録

2026-10-26 の移行。対象は `EngineArchitecture-2026-10-24.md` の M0〜M7。以前の `ArchitectureMigration.md` に記録した段階0〜7とは別の移行である。現在のクラス図は設計資料の第21節を参照する。

## 実装の対応

| 段階 | 現在の実装 |
|---|---|
| M0 | 同一入力・結果hashを持つCPU／実フレーム計測、旧Release実行ファイルの保存、失敗復元の基準 |
| M1 | SceneChangeSet・SceneEdits・型付きScript setterで編集、構造、物理、Animationの成功した変更を捕捉 |
| M2 | SceneDocumentDataを編集の正本とし、SceneData／SceneStorageを実行・GPUから独立 |
| M3 | 世代付きEntityHandle／ComponentInstanceHandle、不変WorldDefinition、型別COWページのWorldState、RuntimeSessionの成功境界 |
| M4 | 不変RenderPacket／PresentationPacket／UiPacket、独立SceneView、GPUの記録・送信・フェンスまで資源を保持 |
| M5 | 変更した部分木の行列更新、数値更新の差分投影、Script段階の借用データ、取引ごとの物理保存点 |
| M6 | SHA-256依存記録、CPU取り込み、非同期要求の世代・取消・公開、完全性検証付き共通成果物とcook |
| M7 | 公開旧SDKの撤去、ABI18、CPU／OS／GPUのターゲット分離、任意のSceneTitle、最小ゲームと既存ゲームの移行 |

実行順は Navigation → Animation → Physics → GPU投影。ホストの `RuntimeSession` がScript・固定時間・物理・予約コマンド・描画候補を一緒に準備する。失敗時は旧世代を保持し、成功時に交換する。GPUへ送信済みの資源はCPU候補の破棄だけで解放せず、フェンス完了まで保持する。

通常の数値更新ではSceneLayout全体を保存型へ書き出さない。構造変更・資源参照変更は、保存IDを解決した定義を準備してから公開する。保存形式と素材IDは維持する。初回の同一値の準備でもアセットIDを不変定義へ解決し、Undoで参照表だけが変わる差異を防ぐ。追加・複製の失敗では生成IDを出力せず、汎用編集APIも通知と同じ採番の個体を公開する。

`RuntimeAnimation` の最終準備でも物理骨姿勢を採用する。複製元の情報は取引の候補内だけに保持し、ホストの複製とScriptのInstantiateで時計・位相・姿勢を新しい個体へ引き継ぐ。失敗した複製は個体・時計・ID採番・通知を公開しない。アセット索引は`ReleaseIndexCache()`で不変の取得済み索引を保ちながら監視ハンドルを解放できる。配布候補の公開前にこれを呼び、Windowsでフォルダーを改名できる状態へ戻す。有効なCPU成果物の再取得ではキャッシュを再保存しない。

## 検証の再現

- `tests/TestSceneLayers.ps1`：依存方向、ソース所有、CPU／描画の境界。
- `tests/TestEngineBuildInputs.ps1`：SDK入力の閉包と出力循環。
- `tests/*Validation.vcxproj`：編集・取引・CPU simulation・パケット・GPU寿命・アセットの失敗注入。
- `tests/TestNativeBuildIdentity.ps1` と `tests/TestScriptSchemaLoading.ps1`：実DLLの構成・ABI・engineBuild・独立JSONスキーマ。
- `tests/RunRegression.ps1`：保存・Editor・Prefab・Script・物理・Animation・描画を通した回帰。
- `tests/TestProjectPackaging.ps1`：作成・cook・マニフェスト・起動・再実行時の上書き拒否。
- `tests/TestPackagedArtifacts.ps1 -Package <既存ゲームの配布先> -Configuration Release`：日本語・空白パスへ複製し、読み取り専用の配布ファイルと空のキャッシュで実CPUローダー／GPU起動を検証。コピー側の成果物を破損させ、配布ファイルを変更せずローカルへ再生成することも確認する。

成果物の検証はチェックサム不正をcache missとして扱う。元データ自体が不正な場合の成功や、任意C++コールバックによる外部副作用の復元は保証しない。

## 最終結果

M0〜M7 の実装と最終検証を完了した。製品ソース169パスを固定して検証し、途中で差分が生じていないことを照合した。公開SDKはABI 18であり、旧SDKを使うゲームのDLLは再ビルドが必要である。

| 検証 | 最終結果 |
|---|---|
| Debug / Development / Release | 各構成でCore 17本・Audit 11本が成功。DevelopmentではApp/Editor再生一致の追加検証も成功 |
| 実DLL | 3構成でビルド識別・ABIの205契約、直接JSONスキーマ読込・差替え、DLL経由の変更捕捉112契約が成功 |
| CPUとGPUの境界 | CPU検証16実行ファイルにD3D11/D3D12/DXGIのimportなし。GPUパケット81/81/80契約が成功 |
| 最小ゲームとGuruttoWorld | 両方のAppが3構成で起動。Editorは対応するDebug / Developmentで両プロジェクトを開く検証が成功 |
| 通し回帰 | Debug 629.8秒、Release 209.0秒で最後まで成功。ビルド時間は含まない |
| 既存ゲームの個別回帰 | SunriseStreet / StreetArrival / ParkAdventure / OceanSunsetが成功。Releaseは最終ソースで4本を通し実行。DebugのSunriseStreetはCPUプレイと描画を分けて確認 |
| 全シーン | Releaseで既存29シーンの保存・再読込、物理、Script、Animation、音声、D3D12描画が成功。再生後の保存データも一致 |
| 配布 | 最小ゲームとGuruttoWorldをDevelopment / Releaseでpackage化し、cook、manifest、起動、既存出力の上書き拒否が成功 |
| 配布先の変更と障害回復 | 日本語・空白パス、空キャッシュ、読み取り専用で成功。CPU成果物221件をローカル再保存なしで再利用し、破損時はローカルへ再生成。Shaderも同じ条件で成功。各配布物1488ファイルの保護を確認 |
| 構成と書式 | 依存方向・ソースの単独所有・SDK入力・XML・フィルター登録が成功。変更対象C++242ファイルを整形確認、git diff --checkも成功 |

Editor.exeのReleaseと、通し回帰実行ファイルのDevelopmentはプロジェクトが提供しない構成である。EditorCoreと共通ライブラリは3構成をビルドした。

既存ゲーム側の変更はSDK移行用の8ファイル。素材・シーン・Scriptを含む元ファイル851件のhashを照合し、ビルド・配布中に書き換わらないことを確認した。Gitの操作によるcommit・stage・pushは行っていない。

## 最終性能比較

他のビルド・回帰を終了させ、保存済み旧Release実行ファイルと新版を交互に3回ずつ実行した。12シナリオすべてで入力hash・最終結果hash・個体数・warmup・計測フレーム数が一致した。各回8フレームをwarmup、64フレームを計測し、以下は各回のmedian/p95/p99の中央値。これは更新処理のCPU計測であり、Draw / Presentやゲーム全体のFPSを含まない。

対象PCはCore i7-12650H、Windows（OS version 10.0.26200）、RTX 4060 Laptop GPU（LUID 00000000:00011792、driver 32.0.16.1088）。実行ファイルのSHA-256、各入力・結果hash、全計測値、phaseの包含時間、プロセス全体のpeak working setをJSONへ保存した。

| SceneWorld経由のCPU更新 | median ms 旧→新 | p95 ms 旧→新 | p99 ms 旧→新 |
|---|---:|---:|---:|
| 空シーン | 0.0039 → 0.0051 | 0.0042 → 0.0054 | 0.0043 → 0.0056 |
| 静的1,000個体 | 1.7731 → 0.0869 | 2.0416 → 0.0928 | 2.3086 → 0.1237 |
| 静的5,000個体 | 11.6372 → 0.3978 | 12.8321 → 0.4570 | 13.0687 → 0.5550 |
| 静的10,000個体 | 24.7952 → 0.7781 | 26.7013 → 0.9854 | 27.6409 → 1.0316 |
| Script 1,000個体 | 5.4643 → 2.6197 | 5.9471 → 2.8585 | 6.2821 → 3.1216 |
| 剛体100個体 | 0.4276 → 0.6056 | 0.5083 → 0.6866 | 0.6755 → 0.9181 |
| 剛体1,000個体 | 6.2712 → 6.6353 | 6.8419 → 7.2216 | 7.1694 → 7.6664 |

CPU単体のScript 1,000個体はmedian 21.0637 → 1.9321 ms。これは旧ScriptRuntimeと、新RuntimeScriptsのFrame・WorldStateのTransactionを比較する別の計測であり、上表のSceneWorld経由の値と混ぜない。

| SceneWorld経由の主スレッドC++割当 | 回数/フレーム 旧→新 | 要求バイト/フレーム 旧→新 |
|---|---:|---:|
| 静的10,000個体 | 30,080 → 93 | 61,271,830 → 347,074 |
| Script 1,000個体 | 32,158 → 25,216 | 9,607,279 → 3,437,499 |
| 剛体100個体 | 1,672 → 1,775 | 873,443 → 610,402 |
| 剛体1,000個体 | 15,190 → 15,313 | 8,929,381 → 5,253,150 |

改善だけでなく退行もある。空シーンでは取引の固定費が約0.0012 ms増え、剛体100個体は約0.178 ms、1,000個体は約0.364 ms遅くなった。新しい世代管理・型別状態同期・取引を含む全体の費用であり、物理演算区間だけを比較して改善済みとは扱わない。割当バイトは減っている一方、物理ケースの割当回数は増えている。phaseの包含時間は重なるため、足して内訳とすることもできない。

プロセス全体のpeak working setの3回の中央値は、CPU単体が195.8 → 263.5 MiB、SceneWorld経由が436.3 → 554.0 MiBだった。全シナリオの準備・保持データと描画デバイスを含むプロセス単位の値であり、フレーム一時領域やGPU常駐量そのものではない。フレームの深い複製を減らしても、分離した定義・実行状態を含むプロセス全体の使用量が減るとは限らない。常駐メモリの構造別内訳、描画・Present、実ゲーム全体のFPSはこの比較では未計測である。

静的シーンとScript多数では、旧全体複製を撤去した効果を確認できた。物理中心の時間とプロセス常駐量は上記の実測値を今後の最適化基準にする。

比較の再現コマンドは tests/CompareRuntimePerformance.ps1 -BaselineFolder generated/architecture-migration-all/legacy-binaries -Runs 3。今回の確定データは [comparison.json](../generated/tests/runtime-comparison/863bdf9c76a540309e64d537d4d21bf1/comparison.json)。全ログと検証結果の索引は generated/architecture-migration-all/complete-verification.json に保存した。
