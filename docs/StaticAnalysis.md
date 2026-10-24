# 静的解析と既存シーンの検証（2026-10-13）

残っていたLizardの129件をすべて解消しました。解析スクリプトの閾値、対象範囲、除外・抑制条件は変更していません。

## 解析結果

|項目|設定・対象|結果|
|---|---|---|
|Lizard|複雑度15、関数長100、引数8|1,998関数、警告0件、終了コード0|
|Cppcheck Debug|実際のMSVC定義、Debugのプリプロセッサー設定|診断0件、終了コード0|
|Cppcheck Development|実際のMSVC定義、Developmentのプリプロセッサー設定|診断0件、終了コード0|
|Cppcheck Release|実際のMSVC定義、Releaseのプリプロセッサー設定|診断0件、終了コード0|

Cppcheckは101の自前C++翻訳単位を解析しています。Engine・App・Editor・SceneRuntime・ContentのScriptが対象で、既存の外部ライブラリー除外を維持しています。MSVC定義はインストール済みコンパイラーから取得した`_MSC_VER=1951`です。

再実行:

```powershell
./scripts/AnalyzeCode.ps1 -Label verified -Jobs 4
```

今回の最終ログは`generated/analysis/*-lizard-verified*.txt`、実行結果は`generated/lizard-analysis-verified.log`です。Cppcheckの3構成の診断ファイルはいずれも空です。

## 修正内容

- Engineの描画準備・GPUバインド、モデル読み込み、アセット移動、入力、音声の検証を処理ごとに分割しました。描画の関連する引数は意味のある設定構造体へまとめ、呼び出し側も更新しました。
- SceneRuntimeのコンポーネント読み書き、配置と参照の検証、物理・Script・アニメーション・UI更新を分割しました。候補の準備と確定、失敗時の復元、GPUリソースの退役順序を保持しています。
- EditorのInspector、UIキャンバス、アニメーション、Material、メニュー、Play入力を分割しました。編集フラグの蓄積と共通の操作履歴追跡を整理し、各ウィジェットの実行順序を保持しています。
- Appの起動・計測・シーン入力と、公園Scriptの開始・固定更新・通常更新を分割しました。シーン設定や既存アセットの値は変更していません。

## 実行検証

すべて終了コード0を確認しました。

|検証|構成・確認範囲|
|---|---|
|全体ビルド|DevelopmentのEngine・SceneRuntime・Editor・AppとネイティブScript|
|検証プログラムとAppのビルド|Debug・Release|
|通常回帰|Debug・Releaseの`ReviewRegressionValidation`|
|既存25シーン|Debug・Releaseで保存・再読み込み後に初期化し、6フレームの物理・Script・アニメーション・音声更新とD3D12描画。保存データの一致、Engineの警告・エラーなし、D3D12のエラー・破損診断なしを確認|
|公園|Debug・Releaseで1,924フレームを通してプレイし、Float・着地・Terraces・Canalと12/12の収集を確認|
|海|Debug・Releaseで旧シーンとの互換性、不正値拒否、HDR・SDR描画、時間変化・停止、カメラ、海面との深度合成、縦長画面、波音を確認|
|機能別回帰|Releaseの26モード、Debugの8モード。ルートモーション・イベント・Script・物理・ラグドール・描画・UI編集・編集データなどを検証。ReleaseではBlend Tree・IK・Prefab・入力・Material・音声・タイトル操作なども検証|
|App実行|Debug・Development・Releaseでシェーダー事前生成、タイトル60フレーム、公園300フレーム。移動・ジャンプ・攻撃・フォーカス喪失と復帰の入力記録とフレーム数を確認|
|シーン変換|`python -m unittest discover -s tests -p test_scene_conversion.py`の8テスト|
|差分|`git diff --check`と各コミットのステージ済み差分確認|

機能別モードは環境変数を各実行後に削除し、独立した検証として実行しました。最終ログは`generated/lizard-tests-release-verified.txt`、`generated/lizard-tests-debug-verified.txt`、`generated/lizard-app-*.log`です。個別出力は`generated/tests/lizard-{Debug,Release,Development}/`にあります。

Debugで検出した中間出力の重複警告は、前日の古いビルド状態ファイルを取り除き、再ビルドで解消を確認しました。

これは自動回帰と入力リプレイの確認です。物理ゲームパッドを使った手操作や長時間連続稼働の確認とは区別します。

## コミットの分割

既存コミットの履歴は保持します。今回の変更は、Engineと描画APIの互換修正、SceneRuntime、Editor・Appと検証記録の3件に分割します。APIを変更するコミットには必要な呼び出し側の修正を含めます。

## 設計上の依存と重複の修正（2026-10-13）

`c166b986`で導入された次の問題を修正しました。既存履歴は書き換えません。

- 公園の初期値・範囲・型付きデータを`ParkScriptSchema.schema.json`へ集約しました。Pythonのシーン生成はJSONを読み、通常ビルド・Scriptビルドは同じJSONからC++の登録情報を生成します。C++の書式を正規表現で解析しません。
- 未設定の数値には登録された初期値をStart・更新時に適用します。保存済みの値を優先し、シーンデータへ書き戻しません。
- Scriptのシーン選択は`dataPurposes`の明示的な宣言に従います。項目名に「scene」が含まれるかで推測しません。SDK構造の変更に合わせABIを5へ更新しました。
- JSONの変更をEditorの自動コンパイルで監視し、生成ヘッダーによる二重コンパイルを避けます。生成処理は内容が変わった場合のみヘッダーを書き換えます。
- アニメーションの表示情報・範囲・反映関数を同じ登録表にまとめ、別のSetter表と常に偽になる条件を削除しました。

静的解析は`AnalyzeCode.ps1 -Label schema-final -Jobs 4`で、Debug・Development・ReleaseのCppcheckとLizardがすべて終了コード0です。閾値はCCN 15・関数長100・引数8のままで、生成ヘッダーも解析対象です。2,004関数、警告0件を確認しました。ログは`generated/schema-final-analysis.log`と`generated/analysis/*-schema-final*.txt`です。

追加した検証は、スキーマとネイティブ登録の一致、既存の編集値の保持、未設定の数値の実行時初期値、用途指定に応じた実際のImGui入力欄、全アニメーション反映関数、生成の冪等性、書式変更・日本語・引用符・改行の文字列生成です。Pythonの公園生成4テストとシーン変換8テストが成功しました。

Developmentの全体ビルドとDebug・Releaseの検証プログラム・Appのビルドが成功しました。3構成のAppでタイトル60フレーム・公園300フレームと入力リプレイを確認しました。ReleaseではネイティブScriptの読み込み・再読み込み・ABI不一致拒否、およびEditorの自動コンパイル・Play中の反映待ち・Stop後の反映も成功しました。

Debug・Releaseで通常回帰、既存25シーンの保存・再読み込みとD3D12描画、公園1,924フレームで12/12収集、海の描画・音声・互換性の回帰が終了コード0です。機能別回帰もDebugの8モード、Releaseの27モードが成功しました。追加したAuthoring検証は両構成で成功しました。ログは`generated/schema-{debug,release}-verified.txt`、`generated/schema-app-*.log`、`generated/schema-script-authoring.log`です。

## 追加調査で再現した6件の修正（2026-10-13）

- 削除済みオブジェクトの所属を同期時に取り除き、別シーンで再利用されたIDが元シーンのアンロードで消えないようにしました。重複所属は読み込みと変更検証で拒否します。
- `SceneSnapshotCache`は`sceneObjects`も比較し、所属だけの変更をUndo・変更判定・復旧用JSONへ反映します。
- Scriptコンパイルの開始・失敗時に以前の反映待ちを破棄します。失敗後にStopしても再生を許可せず、修正後の成功と再読み込みを待ちます。
- アセット参照の解決をモデル・Material・画像・音声・シーン操作などの参照項目へ限定しました。Text・名前・通常のScript文字列は変更しません。Scriptの明示的な参照用途は`sceneAssets`に保存して保持します。構造変更に合わせモジュールABIを6に更新しました。
- スキーマの文字列と項目名にNULが含まれる場合は生成を拒否し、既存ヘッダーを保持します。
- ルートモーション用の衝突ワールドを通常の物理ワールドから独立させて再利用します。座標を同期し、形状・親子関係・構成・ルート・モデルとメタデータ・glTF/GLBの外部バッファが変わった場合は再構築します。無効なColliderは依存ファイルの監視から除外します。

`tests/src/Architecture/DeepStateValidation.h`に所属のID再利用、所属だけのキャッシュ更新、通常の文字列と宣言された参照の改名後の挙動、衝突ワールドの再利用・座標と形状の変更・通常物理への非干渉、外部モデルのバッファ変更と失敗時の候補保持を追加しました。Authoring検証はDebug・Releaseとも成功しました。

Scriptの自動コンパイル検証は専用Contentへ分離し、成功の反映待ち→最新編集の失敗→Stop→修正後の成功を確認しました。ビルド側はContentの祖先からリポジトリを検出し、同じビルドスクリプトと結果保存先を使います。検証用ソースによる並行ビルドへの干渉を解消しました。

最終静的解析は`AnalyzeCode.ps1 -Label deep-fixes-approved -Jobs 4`で、CppcheckのDebug・Development・ReleaseとLizardがすべて終了コード0です。閾値は変更せず、2,018関数・警告0件です。ログは`generated/deep-fixes-approved-analysis.log`と`generated/analysis/*-deep-fixes-approved*.txt`です。

Developmentの全体ビルドとDebug・Releaseの検証プログラム・App・ネイティブモジュールのビルドが成功しました。3構成のAppでタイトル60フレームと公園300フレームの入力リプレイを確認しました。ネイティブScriptのビルド・読み込み・再読み込み・ABI拒否、Pythonの公園生成4テストとシーン変換8テストも成功しました。

Releaseで衝突箱3,000個・移動対象1個の同じ合成条件を計測し、従来の約15ms/回から、初回構築後の再利用時には約6.6ms/回へ短縮しました。計測は描画やシーン全体のフレーム時間を含みません。時間の閾値を回帰テストへ固定せず、安定した条件で再構築が発生しないことと結果の一致を検証します。ログは`generated/deep-fixes-final-benchmark.log`です。

最終コードでもDebug・Releaseの通常回帰、既存25シーンの保存・再読み込みとD3D12描画、公園1,924フレームで12/12収集、海の描画・音声・互換性がすべて終了コード0です。機能別回帰はReleaseの27モードとDebugの8モードが成功しました。ログは`generated/deep-fixes-{debug,release}-final-regressions.log`です。実際のContent用ネイティブモジュールもDebug・Development・ReleaseでABI 6として再ビルドしました。
## 追加調査で再現した9件の修正（2026-10-13）

1. オブジェクト削除ではシーン所属を保持し、削除したIDだけを所属一覧から取り除きます。
2. 複製した個体へ複製元の所属を引き継ぎ、別シーンのアンロードで消えないようにしました。
3. Prefabの保持設定を元データ・保存済み基準・ローカル編集の比較で統合します。個別のOverride表示・リセット・Prefabへの適用・全体リセットも保持設定を扱います。
4. 旧パスが複数のアセットを指す場合もアセット一覧とID解決は利用できます。IDのない曖昧な旧パスを実際に解決する場合だけ、その参照をエラーにします。
5. 解決後の参照表には現在使われているパスとIDだけを記録し、旧ファイル名を再利用した新しいアセットの選択が昔のIDで上書きされないようにしました。未解決の保存データに含まれる有効なIDは保存時に保持します。
6. Terrain・TilemapのComponent編集では描画資源を再構築し、地形の高さ・タイルの変更を表示と選択判定へ即時反映します。
7. モデル移動の依存パス更新で拡張子の大文字・小文字を区別せず、`.GLTF`の外部バッファと`.OBJ`のMaterialを追跡します。
8. 未編集のScriptシーン参照は保存時に既定値と用途を記録し、改名後やモジュールがない状態でも参照を解決できます。編集用の配置データ自体は変更せず、通常の既定値は省略できます。
9. Scriptの登録・モジュール交換の成功時に定義世代を更新し、用途・既定値の変更やモジュール削除でUndo・復旧用JSONのキャッシュを更新します。失敗した登録では世代とキャッシュを維持します。

`tests/src/Architecture/FollowupStateValidation.h`に全9件と境界条件の回帰を追加しました。`WP1_FOLLOWUP_STATE_ONLY=1`の集中検証で、Debug・Releaseの両構成が成功しました。保存・再読み込み後の所属、曖昧な旧パスの局所的拒否とID解決、未解決データの保存、PrefabのOverride操作、地形・タイルの即時選択判定、不正な地形編集の失敗時のデータ・モデル保持を確認しています。集中検証のログは`generated/followup-fixes-{debug,release}-focused.log`です。

最終静的解析は`AnalyzeCode.ps1 -Label followup-fixes-final -Jobs 4`で、CppcheckのDebug・Development・ReleaseとLizardがすべて終了コード0です。閾値はCCN 15・関数長100・引数8のまま、2,022関数・警告0件です。解析対象の除外・警告抑制は追加していません。ログは`generated/followup-fixes-final-analysis.log`と`generated/analysis/*-followup-fixes-final*.txt`です。

Releaseの再リンクで検証用中間PDBの破損を検出し、検証専用の中間ディレクトリを作り直して解消しました。ビルドの警告をエラーとして扱う設定は維持しています。

最終コードでDevelopmentのEngine・SceneRuntime・Editor・App全体ビルド、Debug・Releaseの検証プログラム・Appビルドが成功しました。実際のContent用ネイティブScriptもDebug・Development・Releaseで再ビルドしています。今回の変更ではScript SDK構造のサイズを変更しておらず、ABI 6を維持しています。

Debug・Releaseで通常回帰、既存25シーンの保存・再読み込みと物理・Script・アニメーション・音声更新・D3D12描画、公園1,924フレームで12/12収集、海の描画・音声・互換性がすべて終了コード0です。機能別回帰はDebugの8モードとReleaseの27モードが成功しました。ログは`generated/followup-fixes-{debug,release}-regressions.log`です。

3構成のAppでタイトル60フレーム、公園300フレームと移動・ジャンプ・攻撃・フォーカス喪失／復帰の入力リプレイを確認しました。隔離したScript自動コンパイル検証、ネイティブモジュールの読み込み・再読み込み・旧コールバックの寿命保持・ABI不一致拒否も成功しました。Pythonの公園生成4テストとシーン変換8テストも成功しています。ログは`generated/followup-fixes-app-*.log`、`generated/followup-fixes-script-authoring.log`、`generated/followup-fixes-native-validation.log`、`generated/followup-fixes-python-*.log`です。

既存履歴を保持し、シーン編集とPrefab、アセット参照とモデル移動、Scriptの参照と定義キャッシュ・回帰テストの3コミットに分割します。

## ナビゲーションの座標変換とアセット移動履歴の修正（2026-10-13）

追加調査で再現した4件を修正しました。

1. 親子のNavAgentは同じ更新前の配置から移動先を計算し、移動後の親行列で子のローカル位置へ変換します。通常オブジェクトを挟んだ階層や逆順のオブジェクト一覧でも、子の移動に親の移動が二重加算されません。予定位置だけを保持し、シーン全体の追加コピーを避けています。
2. 障害物の判定では、ワールド単位のAgent半径と高さを障害物の逆行列で投影します。親の拡大・縮小・反転を含めて同じ半径で判定します。
3. 最大傾斜は地形の接線をワールドへ変換した後の法線から求めます。親の縮尺と回転を反映し、回転した平面のNavMeshにも同じ最大傾斜を適用します。
4. アセットの旧パス履歴は重複を追加せず、既存の1024件上限を維持します。新しい履歴が上限を超える移動はディレクトリ作成・モデル移動の前に拒否し、元のファイル・ID・履歴を保持します。メタデータの直接保存でも上限とパスの妥当性を検証し、読み込めない`.meta`を生成しません。

`tests/src/Physics/NavigationTransformValidation.h`に親子の逆順・中間親・通常の子孫・親の回転と非一様縮尺・拡大／縮小／反転した障害物・縮尺のある地形・回転した平面・失敗時の配置保持の回帰を追加しました。`tests/src/Assets/AssetDatabaseValidation.h`には履歴上限での移動拒否と元データ保持、不正な履歴の保存拒否、上限でも既知の旧パスを再利用できることの回帰を追加しました。Debug・Releaseの`WP1_GENRE_ONLY`と`WP1_ASSET_ONLY`がすべて終了コード0です。

最終静的解析は`AnalyzeCode.ps1 -Label navigation-fixes-final -Jobs 4`で、Debug・Development・ReleaseのCppcheckとLizardがすべて終了コード0です。CCN 15・関数長100・引数8、解析対象・警告抑制は変更していません。2,025関数・警告0件です。ログは`generated/navigation-fixes-analysis.log`と`generated/analysis/*-navigation-fixes-final*.txt`です。

最終コードのDevelopment全体ビルド、Debug・Releaseの検証プログラムとAppのビルドが成功しました。DevelopmentのEditorの再リンクで一時的なLNK1104が発生しましたが、再ビルドで正常にリンクできています。既存のContent用ネイティブScriptも各構成のビルドで成功し、SDKのABI 6を維持しています。ビルドログは`generated/navigation-fixes-{debug,release,development}-final-build.log`です。

Debug・Releaseで通常回帰、既存25シーンの保存・再読み込みと物理・Script・アニメーション・音声更新・D3D12描画、公園1,924フレームで12/12収集、海の描画・音声・互換性がすべて終了コード0です。機能別回帰はDebugの8モードとReleaseの27モードが成功しました。ログは`generated/navigation-fixes-{debug,release}-regressions.log`です。

3構成のAppでタイトル60フレームと公園300フレーム、移動・ジャンプ・攻撃・フォーカス喪失／復帰の入力リプレイを確認し、すべて終了コード0でした。ログは`generated/navigation-fixes-app-{Debug,Development,Release}.log`です。検証中に追加された別作業のプロジェクト分離・ビルド設定の変更は、本節の2コミットに含めません。


## 2026-10-16 残存指摘の全解消

Cppcheckに残っていたstyle／performanceの11件と、Lizardの複雑度指摘22件を解消しました。素材の移動先検証・依存参照の書き換え、骨格キー読込、SRV構築、入力の分類、シーン所属、カメラの衝突回避と照準、プレイヤーの発見・ヒント更新などを責務別に分割しています。設定画面への遷移完了フラグは、読み取りとリセットを明示しました。Script SDKの構造は変更せず、ABI 8を維持しています。

`AnalyzeCode.ps1 -Label all-verified-20261016 -Jobs 2`で、Debug・Development・ReleaseのCppcheckとLizardがすべて終了コード0・指摘0件です。CCN 15・関数長100・引数8、解析対象と警告抑制は変更していません。解析対象は2,150関数です。

3構成の本体・ゲームDLLのビルドが成功し、隔離した新規出力先で通常回帰も完走しました（Debug 810.2秒、Release 179.8秒）。両構成で各10モードの重点回帰、街歩き・市街地の通しプレイと実描画、音声ライフサイクルが成功しています。街歩きは5,186フレームの通しプレイと2,330フレーム・156キャプチャの描画、市街地は5,064フレームと短い街区2,635フレームの検証を含みます。Pythonの41テスト、Shaderキャッシュ・GPU資源共有・差分更新と失敗時の保持、Developmentの実App／Editor共通再生・CONFIG往復・START／QUIT演出の検証も成功しました。

外部ゲームの検証でMSPDB140.DLLの不一致を再現したため、配布ビルドと外部ゲーム検証のMSBuildホスト指定をx64に統一しました。修正後は外部プロジェクトの3構成ビルド・起動検証、Editorでの読込と不正version拒否が成功しています。ReleaseとDevelopmentの実パッケージを作成し、DevelopmentはEditorと同じWindows PowerShell経由でも確認しました。完成した両パッケージと3構成の本体は、別の作業フォルダーからの起動検証も終了コード0です。公園検証の破損した既存PDBは既定の中間出力を再生成し、通常の出力先で再ビルド・GPU検証を確認しています。

検証記録とログは`generated/all-fixes-20261016/verification.json`および同フォルダーに保存しています。
