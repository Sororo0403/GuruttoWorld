# シーン変更捕捉API

2026-10-26時点の公開境界。編集・保存は `SceneDocumentData`、実行は `RuntimeSession` / `WorldState`、描画は不変の `RenderPacket` を使う。全体設計と段階移行の目的は [設計案](EngineArchitecture-2026-10-24.md) を参照する。

## 読み取りと変更

| 所有者 | 読み取り | 変更入口 |
|---|---|---|
| 編集ドキュメント | `SceneDocumentData::Read()` の保存IDベースの `WorldReadView` | `SceneEdits` の型付き要求、`SceneWorld` の編集ファサード |
| 実行セッション | `RuntimeSession::Read()` の `WorldState::ReadView` | `Edit`、`Tick`、`TickFixed`、`Animate`、`ApplyEffects`、`ReloadAssets` |
| 実行用取引 | `WorldState::Transaction::Read()` | `SetLocalTransform`、`SetComponent<T>`、`SetRepeated<T>`、`ApplyStructure` |
| Script | `RuntimeScriptContext::Read()`、`scene.Find(id)` | `SetLocalTransform`、`SetComponent<T>`、`SetRepeated<T>`、`SetParent` と構造・副作用予約 |
| 描画 | `RenderPacket` / `PresentationPacket` | 抽出・資源準備の成功後に新しい不変世代を公開 |

保存IDは保存・参照用、`EntityHandle` はWorld識別子・スロット・世代を持つ実行用である。同じ保存IDを削除後に再利用しても古いハンドルは失効する。`ComponentInstanceHandle` は同じ個体内のコンポーネント再生成も区別する。

ReadViewは所有者を延命しない借用値であり、次の確定まで使う。Scriptではコールバック内に限定する。Transformとコンポーネントの取得結果は独立した値で、直接書き換えてもWorldへ反映されない。確定後も保持する値には `ComponentLease<T>` / `RepeatedLease<T>`、描画にはパケットの資源leaseを使う。

`SceneEditResult::succeeded` が候補への受理を表す。同一値への設定は成功だが `changes.Empty()` になる。ScriptのTransformは同じ段階の候補へ反映し、コンポーネント・構造予約は段階末に反映する。同じ型を複数回予約すると最後の一覧を採用し、異なる型の予約は合成する。

## 変更集合

| 格納先 | 内容 |
|---|---|
| `transforms` | 個体IDとPosition・Rotation・Scale・Parentのフィールド集合 |
| `objects` | 個体の追加・削除 |
| `components` | 個体ID、安定したコンポーネントID、型名、追加・削除・値変更 |
| `fields` | 名前・永続化・Prefab・保存設定・参照・順序・実行状態など |
| `resources` | コンポーネントが参照する素材の変更前後。参照IDの再結合も含む |
| `structureChanged` | 個体・コンポーネントの増減、ID交換、順序、親子、シーン所属など |

全コンポーネントの比較と参照走査は `ComponentDefinitions` と参照visitorを共用する。Materialのroughnessだけを変えて参照先が同じ場合、コンポーネント変更は通知するが資源の依存辺は増減しない。GPU同期は資源準備側が判断する。

子孫が親の姿勢に追従した場合、子のローカルTransform変更としては記録しない。WorldStateがdirtyな子孫へ行列を伝播する。確定前後で元の値に戻った保存値は通知しない。物理進行・Animator評価などの内部状態には保守的な無効化も使う。

複数の成功操作にまたがる未取得通知は履歴の和集合である。`TakeChanges()` は一度だけ取り出す。準備中の再入取得は空を返し、確定済みの未取得通知を保持する。

## 成功境界

通常の数値更新は型別ページの変更候補だけを作り、不変のWorldDefinitionや全SceneLayoutを複製しない。構造変更では保存型の候補を作って再コンパイルする。資源参照を変える要求はホストの定義準備で素材ID・設定・参照先を解決してから確定する。数値だけの定常更新は素材のファイルI/Oを行わない。

新しいScript個体は、開始・更新・イベントなどのコールバックを実行した後に失敗した場合だけ`stop`で後始末する。コールバックを一度も実行していない未公開候補には`stop`を呼ばない。公開済み個体の除去・置換は成功後に一度だけ停止する。

RuntimeSessionはScript、Navigation、Animation、Physics、描画投影の候補を同じ取引へ参加させる。準備失敗ではWorld・Script状態・物理保存点・描画資源・変更通知の旧世代を保持する。確定は準備済み値の交換で行う。音声・UI・シーン操作は取引の成功後に専用予約を渡す。任意のC++が直接実行するファイル書き込み等はこの復元対象に含まれない。

資源再読込はCPU取り込み・候補のrig/物理形状・GPU資源を準備してから一括公開する。同じrig成果物ならAnimationの時計を維持し、変更されたrigは再検証する。失敗時には旧パケットと旧資源leaseを引き続き使える。GPU送信後の失敗をCPUの復元だけで取り消したとは扱わず、使用した資源をフェンス完了まで保持する。

## Script SDK

現在のSDK ABIは **18**。`ScriptModuleApi.h` は `RuntimeScriptContext`、`RuntimeScriptDefinition`、`RuntimeScriptScene` とハンドルのサイズを照合する。同じエンジンcheckout・MSVC toolset・構成でSDKとゲームDLLを再構築する。ABI・engineBuild・構成が不一致のDLLは登録前に拒否する。

旧 `ScriptApi.h` / `ScriptRuntime.h` は公開SDKから撤去した。過去の回帰fixtureだけが `tests/src/Support/LegacyScripts` のテスト専用adapterを使う。ゲームの処理は読み取り値を取得して型付きsetterへ渡す。可変の `context.object` は使わない。

```cpp
auto transform = *context.Read().Transform(context.entity);
transform.position[0] += context.Value("speed") * static_cast<float>(context.seconds);
if (!context.SetLocalTransform(transform).succeeded)
{
    throw std::runtime_error("Invalid transform");
}
```

## 検証

`ChangeCaptureValidation` は編集・親子・参照・Script段階・Pause・物理・Animation・失敗復元・通知取得を検証する。`WorldStateValidation`、`RuntimeScriptsValidation`、`RuntimeSessionValidation` が世代・型別更新・取引を、`GpuPacketValidation` がパケット寿命・フェンス・失敗時の再試行を検証する。`AssetArtifactValidation` は依存・hash・cache破損・非同期世代・cook形式を検証する。

実DLLの検証には `tests/fixtures/ChangeCaptureScript.cpp` をゲームの `Assets/Scripts` に置いて再構築し、変更捕捉の実行ファイルへ素材ルートを渡す。DLLは不変の世代名で公開するため、再実行には新しい `ProjectTool --run-id` を指定する。

過去のM1完了記録は `generated/change-capture-complete`、M0〜M7の移行と検証記録は `generated/architecture-migration-all` に保存する。各記録は取得時点のソースに対応し、最新の検証状況は同ディレクトリーの結果を参照する。
