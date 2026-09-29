# カメラ・パーティクルの検証

リポジトリのルートを作業ディレクトリにして実行します。

```powershell
MSBuild tests/CameraParticleValidation.vcxproj /p:Configuration=Debug /p:Platform=x64
generated/tests/outputs/x64/Debug/CameraParticleValidation/CameraParticleValidation.exe
```

Release も同様に検証できます。WARP デバイスで、カメラの行列・不正入力、複数テクスチャグループの初期化、発生・寿命・上限を検証します。アプリの描画確認は別途 Debug / Development / Release で行います。
