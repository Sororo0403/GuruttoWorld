# 開発規約

## フォルダ構成

- `App`：サンプルやゲーム固有の処理、アセット、シェーダー。
- `App/src/DevTools`：アプリ固有の設定・操作パネル。
- `App/Shaders/Common`：複数のシェーダーから読み込む共通処理。
- `Engine/include/Engine`：アプリに公開するヘッダー。
- `Engine/src`：実装。公開ヘッダーと同じ機能分類を使う。
- `Engine/include/Engine/DevTools` と `Engine/src/DevTools`：ImGui の統合やデバッグカメラなど、開発用の共通機能。
- `Engine/externals`：外部ライブラリのソースやライセンス。命名規則の一括変更対象にしない。
- `Engine/build`：エンジンのビルド設定（`.targets` など）。
- `scripts`：依存ライブラリの構築などのスクリプト。
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
| 名前空間 | PascalCase | `Engine`、`App` |
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

## 構成と変更確認

- `Debug`：デバッグ向けの構成。
- `Development`：Engine は最適化あり、App は最適化なし。両方にデバッグ情報を付ける。
- `Release`：配布向けの構成。
- 開発用 UI・カメラの条件は `defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)` とする。
- 移動・改名では include、プロジェクト登録、フィルター、シェーダーの include、アセットのコピー先も更新する。
- 共通コードやプロジェクト設定を変更したら、3 構成のビルドを確認する。
- 再利用するテストソースを追加する場合は `tests` などの管理対象に置き、結果だけを `generated` に出力する。
- 実行時のログ・クラッシュ記録はユーザーの `LocalAppData/WP1/logs`・`LocalAppData/WP1/crashes` に保存する。アプリの配置先に書き込み権限を要求しない。
