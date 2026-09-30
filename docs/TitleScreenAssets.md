# タイトル画面の採用素材

## 制作方針の変更（2026-09-30）

ユーザー指定により、AI 遠景画像を不採用とし、景色は近景から遠景まで CC0 モデルで制作する。未コミットの DistantWorld.png、生成プロンプト文書、画像の読み込み・合成処理、プロジェクト登録を削除した。検討用の TitleScreenReference.png は構図資料としてのみ残し、ゲーム内では使用しない。

## CC0 の遠景（段階4）

既存の Kenney City Kit (Commercial) 2.1 の取得 ZIP から、Models/OBJ format/building-skyscraper-a.obj/.mtl と building-skyscraper-e.obj/.mtl の計4ファイルを無編集で追加した。保存先は App/Assets/Models/Title/Commercial。既存の Textures/colormap.png、License.txt、下記配布元・取得記録を共有する。

- 高層モデル a：元座標の高さ 2.88。左右の遠景に4棟を配置。
- 高層モデル e：元座標の高さ 4.08。通りの奥 (2, 0.08, 94) に、拡縮 (5, 10, 5) で1棟を配置。環状の塔の再現ではなく、既存CC0高層ビルを目印にしている。
- 既存建物 e/k を遠景に4棟追加。近景10棟と合わせて計19棟。
- 道路を24枚、歩道を48枚へ延長。地面は幅160・奥行180に拡大し、遠景も接地させる。橋3枚は維持する。
- 遠クリップを220に拡大。背景は青色のクリア色とし、AI画像・スカイボックス画像は使わない。雲・植生は後続段階。

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

段階2の初期表示ではモデルを2倍に拡大し、Y軸に0.55ラジアン回転させた。段階3では建物と道路の基本倍率を4倍に統一した。元モデルの単位を実寸メートルとは仮定しない。

このモデルは不透明の色テクスチャを使う。植物の透過、PBR、法線マップの対応確認まで完了したことは意味しない。色・構図・装飾は後続段階で制作仕様に近づける。

AI のランタイム素材・タイトル専用音源・新規フォントは、この段階では未採用。

### 街並み用に追加した建物（2026-09-30）

同じ取得 ZIP から building-e.obj/.mtl、building-h.obj/.mtl、building-k.obj/.mtl を Commercial フォルダーへ追加した。全6ファイルを無編集で採用し、既存の Textures/colormap.png と License.txt を共有する。

## 道路・橋：Kenney City Kit (Roads) 2.1

- 作者・配布元：Kenney
- 配布ページ・CC0 表記確認先：https://kenney.nl/assets/city-kit-roads
- 取得元：https://kenney.nl/media/pages/assets/city-kit-roads/74288c9459-1787042796/kenney_city-kit-roads.zip
- 取得日：2026-09-30
- ZIP の SHA-256：22058AF3D68173A7CF9BDA9F0E243A8CEF6BD68168C302EBC76327063849674E
- 配布ページと同梱 License.txt で CC0 を確認した。
- 保存先：App/Assets/Models/Title/Roads
- 同梱ライセンス：[License.txt](../App/Assets/Models/Title/Roads/License.txt)

Models/OBJ format 内の road-straight.obj/.mtl、road-bridge.obj/.mtl、tile-low.obj/.mtl、Textures/colormap.png、および ZIP 直下の License.txt を無編集で採用した。MTL とテクスチャの相対参照を維持している。

road-straight と tile-low は X/Z が -0.5～0.5、Y が 0～0.02。通常倍率4でタイル幅4、上面高さ0.08となる。road-bridge は Y が0～0.52で、Y方向倍率8により上部を約4単位の高さに置く。道路は90度回転して通りに沿わせ、橋は横断方向に3枚接続する。tile-low は歩道と周囲の地面にも使用する。

## 街並みの配置

TitleEnvironment に建物10棟、道路14枚、歩道28枚、橋3枚、地面1枚を配置した。同じモデルは ModelManager を通して共有する。道路に面して建物を左右交互に置き、近景の建物から橋と通りの奥へ視線が抜ける構図とした。

カメラは (-0.8, 1.8, -7.0)、ヨー0.03、ピッチ0.13ラジアンの固定視点。16:9より狭い画面では縦方向の画角を広げ、横方向の構図を維持する。遠景・植物・正式なロゴとメニューは後続段階で追加する。

### 段階3の検証

- 最終の道路・橋の向き修正後に Debug・Development・Release ビルド成功。
- 向き修正前の Debug 回帰検証でタイトル初期化・描画・GPU処理完了に成功。
- Release 実機で建物・道路・橋と既存のタイトル案内の表示を目視した。道路の向きの問題を確認し、修正した。
- その後ユーザーの Escape キーで Computer Use が停止したため、向き修正後の目視、サイズ変更、Enter／Escape の往復操作は未確認。

## 再取得

上記 ZIP の Models/OBJ format から OBJ・MTL と Textures/colormap.png を同じ相対関係でコピーし、License.txt を添える。ZIP と未採用モデルは generated/title-assets にのみ置く。通常のビルドは採用ファイルを App の既存 CopyModels ターゲットで配布先へコピーするため、再ダウンロードは不要。

## 組み込みの検証（2026-09-30）

- Debug・Development・Release のアプリビルド成功。
- 既存 ReviewRegressionValidation を Debug・Release で実行し成功。非表示ウィンドウで、建物を含むタイトルの初期化・描画呼び出し・GPU 処理完了を確認した。
- 採用4ファイルと元データ、および3構成の出力先へのコピーが同一ハッシュであることを確認した。
- Computer Use によるアプリ操作が許可されなかったため、画面の目視と Enter／Escape による往復操作は未確認。上記の自動検証は色・構図・操作感を保証するものではない。
