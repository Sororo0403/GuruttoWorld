# シーン変更捕捉API

2026-10-25時点のM1の変更捕捉境界。Transform、コンポーネント、個体の構造、資源依存、
Script・Animator・物理の実行状態を、成功した操作の後だけ公開する。
実行データの分離や描画抽出など、M2以降の移行は`EngineArchitecture-2026-10-24.md`を参照する。

## 読み取りと変更

| API | 契約 |
|---|---|
| `SceneWorld::Read()` / `ScriptScene::Read()` / `SceneDocumentData::Read()` | 次の変更まで有効な借用`WorldReadView`。Scriptではコールバック中だけ使う |
| `WorldReadView::Transform(id)` | 位置・ラジアン回転・スケールの値を取得。欠損IDは未設定 |
| `WorldReadView::Object(id)` / `Component(id, member)` | 個体または型付きコンポーネントの独立した値を取得 |
| `WorldReadView::ObjectIds()` / `Settings()` | 個体の順序と保存設定を値として取得 |
| `SceneWorld::ApplyLocalTransform(id, transform)` | 親子と投影の準備成功後に確定し、`SceneEditResult`を返す |
| `SceneWorld::ApplyComponents(id, components, root, error)` | ID・親子・姿勢を維持してコンポーネントだけを置き換える |
| `ScriptContext::SetLocalTransform(transform)` | 所有個体の候補を変更し、未確定の変更結果を返す |
| `ScriptContext::SetComponents(components)` / `SetComponent(member, value)` | 段階末への変更予約と、未確定の変更結果を返す |
| `ScriptScene::SetParent(id, parent)` / `ScriptContext::SetParent(parent)` | ワールド姿勢を保つ親変更を段階末へ予約する |
| `SceneWorld::SetScriptsPaused(paused, error)` | Pause候補の準備成功後だけ配置・実行状態・通知を確定する |
| `SceneWorld::TakeChanges()` | 確定した通知を一度だけ取得。準備中の再入では空を返し、既存の未取得通知を保持する |

`SceneEditResult::succeeded`が成功を表す。同一値への設定は成功だが`changes.Empty()`になる。
Transform・コンポーネント・保存設定の同一値設定では投影を作り直さない。
保存データに変化のないPauseは、描画資源を再準備せず実行状態だけを確定する。
EditorのローカルTransform編集と組込`FollowTarget`は新APIを使う。

ビューは所有者を延命しない。一時`SceneLayout`からの直接構築は禁止する。
取得した個体・コンポーネント・Transformは値であり、後から書き換えてもWorldへ反映されない。
Scriptの予約したコンポーネント変更は段階末までReadViewから見えない。
`SetComponent`を異なる型へ続けて呼んだ場合も、同じ段階の予約を合成する。
同じ型へ設定する場合はコンポーネント全体の置き換えであり、後の設定を優先する。

## 変更集合

| 格納先 | 内容 |
|---|---|
| `transforms` | 対象IDと`Position`・`Rotation`・`Scale`・`Parent`のフィールド集合 |
| `objects` | 個体IDと追加・削除の種別 |
| `components` | 個体ID、安定したコンポーネントID、型名、追加・削除・値変更 |
| `fields` | 個体の名前・永続化・Prefab、保存設定、参照表、順序、Script実行状態などのフィールド |
| `resources` | 個体・コンポーネントと、変更前後の素材参照。シェーダーと参照IDの再結合も含む |
| `structureChanged` | 個体・コンポーネントの増減、IDの交換、並べ替え、親子、シーン所属・Prefab接続口の変更 |

全コンポーネントの比較と素材参照走査には、既存の`ComponentDefinitions`と参照visitorを使う。
コンポーネント内部の汎用reflectionは導入せず、コンポーネント単位で変更を通知する。
コンポーネントIDの交換は削除と追加として扱い、Scriptの並べ替えを別の構造変更として記録する。
`resources`はファイルへの依存の差分であり、GPU同期の要否とは区別する。
例えばMaterialのroughness変更はコンポーネント変更になり、参照先が同じなら素材の依存辺は増減しない。
GPU同期の判断には既存の`SceneResourceChanges`を使う。

子孫が親の姿勢に追従しただけなら、子のローカルTransform変更としては記録しない。
利用側は親変更・親の姿勢変更から必要な子孫へ影響を伝播させる。
保存データとScriptの比較対象の値は、同一フレーム内で元に戻して確定前後に差がなければ通知しない。
Animatorの評価と物理の進行は、内部状態の変化を取りこぼさないよう保守的な無効化通知も使う。
複数の成功操作にまたがる未取得通知は履歴の和集合であり、最初と最後だけの差分ではない。
所有ホストが編集操作後または成功フレーム後に一度取得し、必要な利用先へ配布する。

統合には所有データをコピーしない一時索引を使い、先に予約容量を確保して参照を安定させる。
全体比較でも個体ごとのTransform記録を繰り返し探索せず、個体IDで対応付ける。
不正な寿命変更種別と異なるID間のTransform記録は、変更集合を書き換える前に拒否する。

## 書き込み経路の監査

| 書き込み経路 | 捕捉と確定 |
|---|---|
| Transform・選択範囲・親子編集 | `CommitTransforms`で準備済み候補の差分を確定 |
| コンポーネント・設定・改名 | 変更結果を確保してから値と通知を確定。同一値は通知しない |
| 追加・複製・直接削除 | 個体・コンポーネント・参照の増減と、削除に伴う生存する子の親変更を捕捉 |
| 読込・Undo/Redo・Prefab反映・置き換え | 配置全体の比較。実行をリセットする場合は`runtime.reset`も通知 |
| ScriptのStart・FixedUpdate・Update・LateUpdate・イベント・終了準備 | 候補上の保存データとScriptの保存点を比較し、外側の取引成功後に一度公開 |
| Pause・再開 | コールバック・投影・UI候補が成功した場合だけ状態と通知を確定 |
| Animatorの上書き・解除・評価 | 実行用フィールドの変更を通知。評価した姿勢の無効化は保守的に通知 |
| 直接の剛体インパルス | 物理側の受理成功後だけ対象の実行用フィールドを通知 |
| フレーム内の物理進行・Scriptからの力積 | 姿勢に差がなくても、物理進行または非ゼロの力積の受理を`runtime.physics`で通知 |
| 保存済みAnimationのプレビュー | Transformに加えて色・Material等のコンポーネント変更も捕捉 |
| 資源再読込 | 成功後に`resources.reload`を通知。準備失敗では旧通知も維持 |

入力の受け付け・押下蓄積と、音声・シーン操作の予約は既存の専用キューを使う。
それらを保存データの変更通知へ置き換えない。
`ScriptRuntime::ChangesSince`はインスタンス、内部の数値・型付き状態、Pause、UI予約、Animator予約を比較する。
World破棄時の最終停止には、寿命を終えるWorldに対する取得可能な通知は作らない。
描画投影に接続していない`SceneDocumentData`のコピー・代入は値操作であり、差分が必要な所有者はCPU用`Compare`を使う。
接続中のドキュメントの代入保護は維持する。

## 成功境界と互換性

- 新setterは変更入口で結果を作る。旧`ScenePlacement&`による直接書き込みは確定境界の互換比較で補足する。
- 準備中のWorldへの再入変更と、通知の途中取得は拒否する。
- フレーム失敗時は従来の配置候補・Script・物理の保存点を使い、通知も公開しない。
- 不正な力積を拒否した場合は物理キューの枠も消費しない。
- Scriptの親変更は、段階前のワールド行列から一括変換し、欠損・循環・表現できない姿勢を候補ごと拒否する。
- シーン置き換えと直接削除の終了準備も候補で行い、成功後だけ終了処理を一度呼ぶ。
- 既存の段階順は変えない。自分のScriptを削除した場合の終了処理は、従来どおり次の実際のScript処理段階で行う。
- 通知公開は準備済み値の交換で行う。GPU送信後の障害をCPU状態の復元で取り消したとは扱わない。
- 任意のC++コールバックが直接行う音声デバイス操作・ファイル書込等は復元対象外。新しい副作用は専用予約経路を使う。

Scriptの親変更予約の格納領域を追加したため、SDK ABIは**16**。
既存のゲームDLLは対応する構成のSDKで再ビルドする。旧ABIは登録処理の前に拒否する。
可変な旧SDK APIの撤去、実行表現の分離、通常フレームの全`SceneLayout`複製の撤去は後続段階で行う。

## 検証

`tests/ChangeCaptureValidation.vcxproj`をDebug/Development/Releaseでビルドする。
実行ファイルは`generated/tests/outputs/x64/<構成>/ChangeCaptureValidation/ChangeCaptureValidation.exe`。
`tests/fixtures/ChangeCaptureScript.cpp`をテスト用ゲームの`Assets/Scripts`へコピーしてDLLを構築し、
実行ファイルへその素材ルートを渡すと、Transform・複数コンポーネント・親変更の実DLL経由の動作と旧ABI拒否を検証する。
DLL公開名は不変世代なので、再実行では新しい`ProjectTool --run-id`を使う。

CPU契約は`ArchitectureContractValidation`、実行契約は`ArchitectureAuditFixValidation`にも組み込む。
5,000個体の変更と状態統合、生成・削除・参照・親子、Undo/Redo、終了準備、Pause、
各段階と資源準備の失敗による復元、通知の一度だけの取得を確認する。
姿勢が変わらない力積の通知と、4,096件の不正な力積を拒否した後の正常な受理も確認する。
既存のScript実行回帰と`AssetIndexValidation`も合わせて実行する。

2026-10-25の最終検証では、Debug/Development/Releaseの各構成で、実DLL経由を含む変更捕捉112項目と
設計契約204項目がすべて成功した。実行側の設計監査と資源索引も3構成で成功した。
Script実行回帰はDebug/Release、EditorのビルドはDebug/Development、Playerのビルドは3構成で確認した。
C++の変更21ファイルは`FormatCode.ps1`のチェックに合格した。

当初の`context-index-keeps-layers`失敗は、旧シェーダーパスを`ReferenceId`で正規化していなかった問題として修正した。
資源索引テストの初期素材は保存完了後に監視を始め、監視開始前の遅延通知と実際の外部編集を混同しない。
性能改善値やM0の性能基準全体の完了は主張しない。
ログと集計は`generated/change-capture-complete`に保存する。
