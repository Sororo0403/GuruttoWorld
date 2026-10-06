# カメラ・パーティクルの検証

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

タイトルUIは全構成で App/Assets/Textures/Title/UiAtlas.png を使用します。再生成する場合のみ PowerShell 7で scripts/GenerateTitleUi.ps1 を実行してください。通常のビルドは生成済み画像をコピーします。以前の Title.png は現在のタイトルでは使用しません。

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

タイトル背景は CC0 の街並み・植生と、TitleSky/TitleMesh の専用シェーダーを描画します。ReviewRegressionValidation のタイトル検証ログは generated/tests/title-rendering.log に保存します。シェーダーは実行時にコンパイルするため、ビルド成功だけでなく Debug・Release の検証実行も確認してください。

設定の回帰検証は generated/tests/settings 以下に一時ファイルを作り、ユーザーの設定を変更しません。欠損・不正形式・上下限・上書き・保存失敗・取消と再編集・パッド接続直後の入力を確認します。設定UIの3選択状態と保存失敗文も上記3サイズで描画します。

実機確認ではタイトルの「設定」を開き、左右で音量と背景演出を変更して「保存して戻る」を選びます。アプリ再起動後の復元と、ゲーム開始後にSpace／Aで再生するサンプル音の音量を確認してください。Esc／Bは保存せず戻ります。背景演出OFFでカメラが停止し光の粒が消えること、ONで再開すること、取消で元の設定へ戻ることを確認してください。

タイトル演出の回帰検証は、登場スキップと決定を分離し、キーを離して押し直すまで開始しないこと、非アクティブ中の時間停止、画面を覆った後の一度だけの遷移通知を確認します。登場・ワイプ途中の描画も3サイズで実行します。実機では初回起動、登場中のEnter／A、開始、ゲームからの再訪（登場省略）、終了を確認してください。

背景待機演出は250秒分のカメラ位置範囲・粒の不透明度を検証します。OFFと非アクティブ時の停止、不正時間の無視、3サイズでのON／OFF／再開描画も確認します。

タイトル音声の回帰検証は5つのWAVの読み込み・無音ループ再生・停止と、メニュー効果音イベントの重複抑止を確認します。実機では音量設定の即時反映と取消、0%の無音、フォーカス復帰時のBGM先頭からの再生、ゲーム遷移・終了時の停止を確認してください。音源再生成は scripts/GenerateTitleAudio.py を実行します。

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
