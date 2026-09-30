# タイトル画面の採用素材

## 建物モデル：Kenney City Kit (Commercial) 2.1

- 作者・配布元：Kenney
- 配布ページ・CC0 表記確認先：https://kenney.nl/assets/city-kit-commercial
- 取得元：https://kenney.nl/media/pages/assets/city-kit-commercial/a742d900eb-1753115042/kenney_city-kit-commercial_2.1.zip
- 取得日：2026-09-29
- 取得 ZIP の SHA-256：F8B09B081C2BB88BCC126E2DEC1CB40FD0DAD7E7E591B6C26AAEFE96FB35276B
- 配布ページと同梱 License.txt の両方で Creative Commons Zero（CC0）を確認した。
- 同梱ライセンス：[License.txt](../App/Assets/Models/Title/Commercial/License.txt)

| 採用ファイル | ZIP 内の元ファイル | 用途 |
|---|---|---|
| App/Assets/Models/Title/Commercial/building-c.obj | Models/OBJ format/building-c.obj | タイトル用の最初の建物1棟 |
| App/Assets/Models/Title/Commercial/building-c.mtl | Models/OBJ format/building-c.mtl | 材質と画像の相対参照 |
| App/Assets/Models/Title/Commercial/Textures/colormap.png | Models/OBJ format/Textures/colormap.png | 同梱の色テクスチャ |
| App/Assets/Models/Title/Commercial/License.txt | License.txt | 配布時のライセンス記録 |

採用した4ファイルは元データをそのままコピーしており、変換・編集はしていない。OBJ の mtllib と MTL の Textures/colormap.png という相対参照を維持する。OBJ の元座標範囲は X=-0.441794～0.441794、Y=0～0.893、Z=-0.545～0.545。Y=0 が底面。読み込み時には既存 Assimp 設定で左手系へ変換する。

初期表示はモデルを2倍に拡大し、Y軸に0.55ラジアン回転させる。元モデルの単位を実寸メートルとは仮定しない。後続の街並み配置で統一する。

このモデルは不透明の色テクスチャを使う。植物の透過、PBR、法線マップの対応確認まで完了したことは意味しない。色・構図・装飾は後続段階で制作仕様に近づける。

AI のランタイム素材・タイトル専用音源・新規フォントは、この段階では未採用。

## 再取得

上記 ZIP の Models/OBJ format から OBJ・MTL と Textures/colormap.png を同じ相対関係でコピーし、License.txt を添える。ZIP と未採用モデルは generated/title-assets にのみ置く。通常のビルドは採用ファイルを App の既存 CopyModels ターゲットで配布先へコピーするため、再ダウンロードは不要。

## 組み込みの検証（2026-09-30）

- Debug・Development・Release のアプリビルド成功。
- 既存 ReviewRegressionValidation を Debug・Release で実行し成功。非表示ウィンドウで、建物を含むタイトルの初期化・描画呼び出し・GPU 処理完了を確認した。
- 採用4ファイルと元データ、および3構成の出力先へのコピーが同一ハッシュであることを確認した。
- Computer Use によるアプリ操作が許可されなかったため、画面の目視と Enter／Escape による往復操作は未確認。上記の自動検証は色・構図・操作感を保証するものではない。
