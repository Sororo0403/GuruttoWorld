# タイトル画面の採用素材

## ロゴ・メニュー（段階7、2026-09-30）

- TitleUi がロゴ、サブタイトル、帯、矢印、日本語ラベル、Enter案内を別々に配置する。1280×720を基準に縦横比を維持して拡縮し、余白を中央へ配分する。
- 仮タイトルは NEW WORLD、サブタイトルは「まだ見ぬ世界へ」。AI画像は使わず、フォントの輪郭と自作の幾何図形で制作した。
- 素材：App/Assets/Textures/Title/UiAtlas.png（2048×1024、透明PNG）。全UI部品を1枚にまとめ、UV範囲を切り出して描画する。
- 再生成：PowerShell 7で `pwsh -NoProfile -File scripts/GenerateTitleUi.ps1`。System.Drawing と同梱フォントを使用する。OSへのフォントインストールやネット接続は不要。通常ビルドは生成済みPNGをコピーする。
- 元の Title.png と GenerateTitleTexture.ps1 は以前のサンプルとして残るが、現在の TitleScene は読み込まない。
- この段階は見た目の実装。Enterで開始する既存操作を維持し、設定・終了は無効表示。上下選択・ゲームパッド・終了処理は段階8、設定画面は段階9で接続する。

### 使用フォント

- M PLUS 1p Black、Copyright 2016 The M+ Project Authors.
- 配布元：https://github.com/google/fonts/tree/main/ofl/mplus1p
- 取得元：https://raw.githubusercontent.com/google/fonts/main/ofl/mplus1p/MPLUS1p-Black.ttf
- ライセンス取得元：https://raw.githubusercontent.com/google/fonts/main/ofl/mplus1p/OFL.txt
- 取得日：2026-09-30。SIL Open Font License 1.1（同梱 OFL.txt で確認）。CC0モデルとは別のフォント素材として記録する。
- 保存先：App/Assets/Fonts/MPlus1p/MPLUS1p-Black.ttf と OFL.txt。フォントは無編集。
- TTFのSHA-256：815821A62CE085E453AF318CA004768E336329D5A7D6F7BED272E97E7862D43E
- ビルド時は OFL.txt を出力先の Licenses/MPlus1p にコピーする。実行時にフォントファイルは読み込まず、生成済み文字画像を使う。

### 段階7の確認

Release実機でロゴ・各ラベル・帯・矢印・操作案内と、背後の街並みを確認した。サイズ別の自動描画検証は1280×720、1024×768、720×1280を対象とする。自動検証は文字の視認性の目視評価を代替しない。

## 色調と奥行き（段階6、2026-09-30）

- TitleSky.hlsl で地平線から上空へ青くなるグラデーションを描く。画像素材は使わず、既存スプライト機能の白1ピクセルテクスチャを描画用に初期化する。
- TitleMesh.hlsl 経由でのみ距離の霞を有効にする。カメラから24～155ワールド単位で滑らかに増え、最大混合率0.72で空の色へ近づける。通常の Mesh.hlsl を使うゲーム本編には適用しない。
- 空と霞の共通色は Common/TitleAtmosphere.hlsli に集約する。光は暖色 (1, 0.96, 0.86)、環境光0.48、直接光0.72、ハイライト0.03に調整。
- Nature の草・低木・花の grass 材質を Kd=(0.28, 0.58, 0.16)、木の leafsGreen を (0.22, 0.48, 0.12)、woodBark を (0.38, 0.24, 0.12) に変更。対象は grass_large.mtl、plant_bush.mtl、flower_yellowA.mtl、tree_small.mtl。OBJ とライセンスは無編集。以下の段階5の「無編集」は採用時点の記録。
- 実機で空と遠景の霞を確認した。植物の元色が青緑に強く寄っているのを見て上記材質色を調整したが、その後ユーザーの Escape キーで Computer Use が停止し、最終材質色の目視は未完了。
- Debug・Release の回帰検証で、最終のシェーダーと材質を使った初期化・描画・GPU処理完了に成功。Debugでのみ発生したシェーダーコンパイル警告も修正済み。
- 見た目を調整する際はこの色・霞の値を変更する。今回の効果は大気の色混合であり、影・PBR・環境マップの追加ではない。

## 植物・看板（段階5、2026-09-30）

### 植物：Kenney Nature Kit

- 作者・配布元：Kenney
- 配布ページ・CC0 表記確認先：https://kenney.nl/assets/nature-kit
- 取得元：https://kenney.nl/media/pages/assets/nature-kit/37ac38a37b-1677698939/kenney_nature-kit.zip
- 取得日：2026-09-30
- ZIP の SHA-256：FA7974A0D342BFE63C38664BA9F8EC1A4AAB8EA25F099BDC56870E33588C4D9D
- 配布ページと同梱 License.txt の両方で CC0 を確認。同梱文書のバージョン表記は Nature Kit (2.1)。
- 保存先：App/Assets/Models/Title/Nature
- 同梱ライセンス：[License.txt](../App/Assets/Models/Title/Nature/License.txt)

Models/OBJ format から grass_large.obj/.mtl、flower_yellowA.obj/.mtl、tree_small.obj/.mtl、plant_bush.obj/.mtl を無編集でコピーし、ZIP 直下の License.txt を添えた。MTL は Kd の材質色を指定し、画像を参照しない。透明画像を使わず、草・葉・花はメッシュとして描画する。透過テクスチャ対応を追加したわけではない。

元モデルの高さは草0.254、花0.1925、木1.110037、低木0.2444436。歩道上面 Y=0.16、建物の屋上、橋面に合わせて倍率を変えた。AddGreeneryAndSigns に草32、花12、木6、低木35の計85個を配置。同じモデルは ModelManager で共有し、配置は乱数に依存しない。

### 看板：取得済み City Kit (Roads) 2.1

既存の Roads 取得 ZIP から Models/OBJ format/road-sign-street.obj/.mtl と road-sign-empty.obj/.mtl を無編集で追加。App/Assets/Models/Title/Roads に置き、同梱の colormap.png と License.txt を共有する。出典と取得記録は下記の道路素材の節を参照。

看板は手前右と左奥に1本ずつ配置。独自の文字・ロゴは追加していない。

### 検証状況

- Debug・Development・Release ビルド成功。
- Release を起動したが、ユーザーの Escape キーで Computer Use が停止したため、配置後の目視と操作確認は未完了。屋上・橋の低木の接地と、看板の向きは実機の目視確認が残る。

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
