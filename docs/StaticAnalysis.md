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
