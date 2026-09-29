# カメラ・パーティクルの検証

リポジトリのルートを作業ディレクトリにして実行します。

```powershell
MSBuild tests/CameraParticleValidation.vcxproj /p:Configuration=Debug /p:Platform=x64
generated/tests/outputs/x64/Debug/CameraParticleValidation/CameraParticleValidation.exe
```

Release も同様に検証できます。WARP デバイスで、カメラの行列・不正入力、複数テクスチャグループの初期化、発生・寿命・上限を検証します。アプリの描画確認は別途 Debug / Development / Release で行います。

## 音声の検証

```powershell
MSBuild tests/AudioValidation.vcxproj /p:Configuration=Debug /p:Platform=x64
generated/tests/outputs/x64/Debug/AudioValidation/AudioValidation.exe
```

Release でも実行できます。Media Foundation で既存サンプル WAV を AAC（M4A）に変換し、デコード・無音再生・再生完了・ループ・停止・再初期化を確認します。読み込み後に元ファイルを改名しても再生できることを検証します。生成ファイルは `generated/tests/audio` に保存します。
