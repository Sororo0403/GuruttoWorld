# Assimp

Assimp v6.0.4: https://github.com/assimp/assimp/tree/e0b52347c6e52de2827ec957a9ebf00ce3c54f79

Visual Studio 2026 の C++ 開発環境、Git、CMake 4.2 以降を使用します。
Engine の初回ビルドで scripts/BuildAssimp.ps1 を実行し、固定コミットを確認して静的ライブラリを生成します。
初回取得時はネットワーク接続が必要です。取得後の通常ビルドはオフラインで実行できます。
OBJ インポーターのみを有効化し、エクスポーター・ツール・テストは無効化しています。
ソースは generated/dependencies、CMake 中間生成物は generated/intermediate、ライブラリは generated/outputs に置きます。
Debug と Release でライブラリとランタイムを分けます。Assimp と zlib は Engine.lib に結合します。
第三者コードにはプロジェクト本体の /W4 /WX を適用せず、上流の警告設定を使用します。
LICENSE.txt と zlib-LICENSE.txt を実行ファイルの Licenses フォルダーへコピーします。

OBJ の頂点・UV・法線・複数メッシュ、MTL の Kd と map_Kd に対応します。
法線がない場合は面法線を生成し、テクスチャがない場合は白色の一画素を使用します。
透明マテリアル、埋め込み画像、アニメーションは対象外です。
App/src/main.cpp の modelPath を変更すると別の OBJ を表示できます。
モデルの大きさに合わせてワールド行列またはカメラ位置を調整してください。
