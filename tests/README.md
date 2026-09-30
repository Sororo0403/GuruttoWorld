# カメラ・パーティクルの検証

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

タイトルは全構成で App/Assets/Textures/Title.png を描画します。画像を作り直す場合のみ scripts/GenerateTitleTexture.ps1 を実行してください。通常のビルドでは生成済み画像をコピーします。

タイトル背景は CC0 の街並み・植生と、TitleSky/TitleMesh の専用シェーダーを描画します。ReviewRegressionValidation のタイトル検証ログは generated/tests/title-rendering.log に保存します。シェーダーは実行時にコンパイルするため、ビルド成功だけでなく Debug・Release の検証実行も確認してください。
