# 開発規約

## フォルダ構成

- `Player/include/Player` と `Player/src`：起動・更新・描画ループを提供する共通の静的ライブラリ。ゲームの `main.cpp` と個別の診断処理はゲーム側の `src` に置く。
- `Editor/src`：ゲームエディター。起動処理の `main.cpp` 以外は、素材管理の `Assets`、保存処理の `Documents`、編集状態の `Editing`、`Animation`、`Inspector`、`Project`、`Viewport`、`Ui`、`Playback`、画面共通処理の `Shell` に分ける。
- `SceneRuntime/include/SceneRuntime` と `SceneRuntime/src`：共通のシーン処理。公開ヘッダーと実装を同じ機能分類（`Scene`、`Rendering`、`Animation`、`Audio`、`Physics`、`Scripts`、`Ui`、`Gameplay`、`Project`）に分ける。`SceneData`・`SceneStorage`・`SceneRuntime` のビルド責務は各 `.vcxproj` で管理する。
- 個別ゲームはエンジンの外に置く。ぐるっとワールドは隣の `../GuruttoWorld` で独立管理し、ゲーム側の `WP1.project.json` でエンジンを参照する。
- `Engine/Shaders`：標準描画のシェーダー。ゲームの素材とは別のリソースとして配布する。
- `Editor/Resources/Fonts`：エディター専用フォントとライセンス。ゲームには配布しない。
- `Tools/GameProject/Templates`：新規ゲーム作成用のひな形。
- `Engine/Shaders/Common`：複数のシェーダーから読み込む共通処理。
- `Engine/include/Engine`：アプリに公開するヘッダー。
- `Engine/src`：実装。公開ヘッダーと同じ機能分類を使う。
- `Engine/include/Engine/DevTools` と `Engine/src/DevTools`：ImGui の統合やデバッグカメラなど、開発用の共通機能。
- `Engine/externals`：外部ライブラリのソースやライセンス。命名規則の一括変更対象にしない。
- `Engine/build`：エンジンのビルド設定（`.targets`）とアプリケーションのマニフェスト。
- `scripts`：依存ライブラリ構築、コード整形・解析、エンジンのビルド識別子生成、旧シーン変換などの開発用スクリプト。エディターの主要機能は置かない。
- `tests`：実行スクリプトとテスト用 `.vcxproj`。C++ のテスト本体は `tests/src` の機能別フォルダーに置き、共通補助は `Support`、通し回帰の入口は `Regression`、DLL 用の素材は `tests/fixtures` に置く。
- `Tools`：ビルド時に使用する開発ツール。`Tools/ProjectTool` はゲーム処理のコンパイル、配布物の作成、素材同期、プロジェクト作成・更新を担うC++製CLIで、エディターとMSBuildから共用する。
- `docs`：設計・機能・制作手順の資料。
- `generated`：ビルド出力、中間生成物、取得した依存ライブラリ、一時検証結果。Git 管理しない。

`Graphics` は次の役割で分類する。

| フォルダ | 役割 |
|---|---|
| `DirectX12` | デバイス、スワップチェーン、フレーム管理、GPU 同期 |
| `Resources` | テクスチャ、深度バッファー、頂点・インデックスバッファー |
| `Renderers` | 描画処理と、描画方式ごとに共有するパイプライン |
| `Models` | モデルデータとファイル読み込み |
| `Materials` | マテリアル関連のパラメーター、UV 変換、光源設定 |

`ShaderCompiler` は `Graphics` 直下に置く。分類のためだけに空のフォルダを先に増やさない。
SceneRuntime の公開ヘッダーは `<SceneRuntime/機能名/ファイル名.h>` で参照する。同じ機能の非公開ヘッダーは同じフォルダーから、別機能の非公開ヘッダーは相対パスで参照する。
Visual Studio のフィルターは実際のフォルダ構成と一致させる。
ソースを `Debug` というフォルダに置かない（`.gitignore` のビルド生成物ルールに一致する）。

## 命名規則

| 対象 | 規則 | 例 |
|---|---|---|
| クラス・構造体・列挙型・型エイリアス | PascalCase | `DebugCamera`、`RenderResult`、`SoundHandle` |
| 関数・メソッド | PascalCase | `Initialize`、`GetPosition` |
| ローカル変数・引数 | camelCase | `deltaSeconds`、`cameraPosition` |
| private のメンバー変数 | camelCase + 末尾 `_` | `position_`、`moveSpeed_` |
| 構造体の公開データ | camelCase | `maxDeltaSeconds` |
| 名前付きのコンパイル時定数（`constexpr`） | PascalCase | `BufferCount`、`DescriptorCount` |
| 列挙値 | PascalCase | `RenderResult::Failed` |
| 自作マクロ | UPPER_SNAKE_CASE、エンジン用は `ENGINE_` 接頭辞 | `ENGINE_DEVELOPMENT` |
| 名前空間 | PascalCase | `Engine`、`Player` |
| C++ ファイル | 主な型・機能の PascalCase 名 | `UvTransform.h`、`UvTransform.cpp` |
| 機能フォルダ | PascalCase | `Graphics`、`DevTools` |
| 基盤フォルダ | 小文字 | `src`、`include`、`externals`、`scripts`、`build`、`generated` |

- `const` なローカル変数・引数は通常の変数として camelCase にする。
- 略語は単語として扱う：`GpuSynchronization`、`UvTransform`、`modelUv`、`debugUi`。
- `DirectX`、`ImGui`、`XAudio2` などの固有名と、`Texture2D` のような次元表記は維持する。
- Windows／DirectX／標準ライブラリなどの外部 API・型・メンバー・マクロは元の綴りを使う。
- `main.cpp`、`wWinMain`、演算子、シェーダーのセマンティクスなど、慣例や外部仕様に従う名前は例外とする。
- HLSL の自作関数・型も同じ規則に従う。画面表示や説明文の「UV」「GPU」は識別子ではないので変更しない。

## コメントと書式

- ヘッダーに宣言する関数には日本語の Visual Studio XML コメント（`/// <summary>`）を書く。
- 必要に応じて `param`、`returns` で引数や戻り値を説明する。
- `.editorconfig` に従い、UTF-8（BOM なし）・CRLF を使う。
- C++ はスペース 4 個、プロジェクト設定や XML はスペース 2 個でインデントする。

### コメントの言語と内容

- 自作コードの説明コメントと Python の docstring は日本語にする。識別子、API 名、単位などの技術用語は元の綴りを維持する。
- 外部コードのコメント、ライセンス、原文引用、ツールが解釈する指示（抑制コメントなど）は原文・指定形式を維持する。
- ヘッダーの関数説明は `/// <summary>` に統一する。オーバーロードはそれぞれの違いを説明する。
- 実装内の補足は C++／HLSL では `//`、Python／PowerShell では `#` を使う。処理を逐語的に説明せず、意図・制約・失敗条件・単位を記す。
- 自明な処理へのコメント追加や、翻訳による識別子・ログ・画面表示文字列の変更は行わない。コメントの言語と意味はレビューで確認する。

### C++ の自動整形

- `.clang-format` を書式の基準とし、clang-format **22.1.3** を使う。更新時は設定と代表ファイルの差分を確認してから、使用バージョンも更新する。
- 波括弧は次行、名前空間内も 4 スペース、ポインター・参照は型側（`Type* value`、`const Type& value`）、行幅は 120 桁を目安とする。
- 空の関数以外は複数行で書く。条件分岐・ループの本体も次行に置く。波括弧の追加は制御構造の意味を確認する必要があるため、機械的整形とは別に行う。
- include 順序とコメント本文は自動で並べ替え・折り返ししない。書式変更とコメントの翻訳・説明の修正は別コミットに分ける。
- 対象は Git 管理下の自作 C++。`externals`、`generated`、ビルド出力、`*.generated.*` は除外する。生成コードの書式は生成元で対応する。HLSL は今回の自動整形対象に含めない。

リポジトリルートで実行する。既定は読み取り専用のチェックで、不一致は失敗として終了する。

```powershell
# 選択したファイルを確認（パスはリポジトリ相対）。
./scripts/FormatCode.ps1 -Paths Engine/include/Engine/Core/Validation.h
# レビュー対象のファイルだけを書き換える。
./scripts/FormatCode.ps1 -Paths Engine/include/Engine/Core/Validation.h -Fix
# 全対象を確認。既存コードの移行完了までは不一致が残る。
./scripts/FormatCode.ps1
```

PATH 上の clang-format、または Visual Studio 同梱の x64 版を使用する。別の配置では `-ClangFormat 'C:/path/to/clang-format.exe'` を指定する。
同じチェックを CI でも使用できるが、全体を必須チェックにするのは既存コードの移行後とする。
移行は担当範囲ごとに行い、進行中の未コミット変更と一括整形を混ぜない。

### Python・PowerShell の書式

- Python は 4 スペース、関数・変数は `snake_case`、クラスは `PascalCase` とする。上の C++ 命名表を Python に適用しない。
- PowerShell は 4 スペース、関数は既存の `Verb-Noun` 形式に合わせ、演算子・代入・コンマの後ろに空白を置く。
- いずれも独立した文を 1 行に詰め込まない。専用フォーマッターの導入・既存コードの一括整形は別途行う。

## 構成と変更確認

- `Debug`：デバッグ向けの構成。
- `Development`：Engine は最適化あり、Player とゲーム側の起動処理は最適化なし。デバッグ情報を付ける。
- `Release`：配布向けの構成。
- 開発用 UI・カメラの条件は `defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)` とする。
- 移動・改名では include、プロジェクト登録、フィルター、シェーダーの include、アセットのコピー先も更新する。
- 共通コードやプロジェクト設定を変更したら、3 構成のビルドを確認する。
- 再利用するテストソースを追加する場合は `tests` などの管理対象に置き、結果だけを `generated` に出力する。
- 実行時のログ・クラッシュ記録はユーザーの `LocalAppData/WP1/logs`・`LocalAppData/WP1/crashes` に保存する。アプリの配置先に書き込み権限を要求しない。
