# タイトル画面の採用素材

## 広場の街灯・休憩スペース

- 取得済みKenney City Kit Roadsのlight-square-doubleを5基配置。広場の外周と左歩道に置き、階段へ続く中央の通路を空ける。
- 取得済みKenney City Kit Commercialのdetail-parasol-aを2基、detail-awning-wideを主役の入口に配置。パラソルはテーブル付きのCC0モデル。
- OBJ・MTLを元パックから無編集でコピーし、既存の各パックのTextures/colormap.pngと同梱CC0ライセンスを使用。新規の外部ダウンロードはない。
- 街灯は形状と既存の共通照明で描画する。動的な点光源は追加していない。植物・UI・カメラは変更しない。

## 背景の中央広場・試作

- UIを変更せず、街の右側の中景を中央広場へ変更。既存Kenney CC0の歩道モデルで舗装、低いタイルモデルで三段の階段と基壇を配置。
- 広場の奥にbuilding-hを市庁舎風の主役として配置し、右隣にbuilding-cの低層棟を置く。右手前の建物は外側へ移動し、広場への視線を開ける。
- 高架道路はZ=18からZ=42へ移動し、主役の正面を遮らない配置に変更。左側の建物列・遠景・通常カメラ・植物を置かない方針は継続。
- 新しい外部素材は取得せず、既存モデルの配置と倍率のみを変更。実画面での構図と見栄えは目視確認の対象。

## 現在のタイトル操作

- タイトル画面はロゴと `PRESS ANY BUTTON` のみ表示し、キーボードの任意のキー・ゲームパッドのボタン押下でゲームを開始する。
- 設定・終了メニューと専用施設の配置を撤去。終了ゲートの区画は通常の建物へ戻し、項目によるカメラ移動は行わない。
- 初回フォーカス時の入力を抑止し、開始演出後に一度だけゲームへ遷移する。既存の保存済み音量・背景演出設定は継続して読む。

## 終了ゲート（2026-10-05）

- Kenney Castle Kit 2.0 の `wall-narrow-gate.obj` を採用。門本体のOBJ・MTL・Textures/colormap.pngは無編集で `App/Assets/Models/Title/Exit/Castle` に保存。
- 配布ページ: https://kenney.nl/assets/castle-kit 。CC0表記と同梱License.txtを確認し、同じフォルダーに保存した。
- 取得元: https://kenney.nl/media/pages/assets/castle-kit/a395102d20-1711543616/kenney_castle-kit.zip
- 横道は撤去。門は高架の先の右側一棟分を置き換え、建物と同じ列の(4.8, 0.08, 26)に配置し、正面を通りへ向ける。手前のZ=12の区画には元の建物を復元。高架本体は保持し、両端に重なる右Z=19・左Z=20の建物を撤去。終了の視点は(-0.8, 1.8, 20)、ヨー0.56。門の倍率は4。
- 「また来てください」の追加看板は撤去し、CC0の門本体だけを表示する。

## モデル側で管理する表面材質（2026-09-30）

- 前回追加したTitleSurfaceMesh.hlsl・Common/TitleSurface.hlsliとMesh.hlslの材質推測処理を撤去。全モデルを既存TitleMesh.hlslの共通照明・霞で描画し、材質はOBJのusemtl、MTLのmap_Kd、UVで指定する。
- 元のKenney CC0モデルはCommercial/Roadsに無編集で保持。Surface/Commercialに建物6種、Surface/Roadsに道路・歩道・地面の派生モデルを生成した。頂点位置・法線・面の順序は維持し、材質割り当てと対象面のUVだけを変更する。
- 元パレットの白い壁領域をplaster、道路のアスファルト領域をasphaltに割り当てる。窓・装飾・路面の白線などは元のcolormap.pngと元UVを維持。橋・看板・植物は元モデルのまま。
- Surface/Texturesのplaster.png、asphalt.png、concrete.png、paving.pngは本プロジェクトで生成した256×256の反復可能な低コントラスト画像。写真素材・AI画像ではない。CC0は元のKenney形状のライセンスであり、独自生成画像の外部出典を意味しない。
- scripts/GenerateTitleMaterials.py（Python 3 + Pillow）で再生成する。--checkで全22ファイルの一致を検証できる。通常のビルドや実行にPythonは不要。元モデルの出典・取得記録・ライセンスは本書のKenney採用記録を継承する。
- UVはモデルの主な面に沿って平面投影し、近景で約2ワールド単位ごとに質感画像が繰り返されるようにした。歩道のみ0.8単位の目地に合わせ、幅1.6・長さ4の配置倍率を織り込む。大きな地面は専用ground.objでUV倍率を分けた。別の倍率で使う場合はモデル側のUVを調整する。
- 現行のテクスチャローダーは単一ミップ。前回のシェーダーによる距離別の質感抑制は撤去している。細かすぎる模様を避けたが、遠距離・移動中のちらつきと各サイズの見え方は目視確認が必要。
- Debug・Development・ReleaseのビルドとDebug・Releaseの回帰検証が成功。9派生モデルの頂点・法線・面の頂点順序が元データと一致し、全MTLの画像参照が存在することを検証した。再生成の--checkも成功。
- 法線マップやPBRの追加、モデルの形状・街の配置の変更は行っていない。

## BGM・操作効果音（段階12、2026-09-30）

- 外部サンプルや既存楽曲を使用せず、scripts/GenerateTitleAudio.pyで波形・音列を生成したオリジナルの仮音源。モデル素材のCC0とは別に、本プロジェクト内で生成した素材として記録する。
- App/Assets/Audio/Title/Bgm.wav：120 BPM、8小節・16秒のループ。短い鍵盤風コード、ベース、控えめな打楽器。Select.wav、Confirm.wav、Back.wav、Error.wav：選択・決定・取消・保存失敗の短い電子音。
- 全てモノラルPCM16、32 kHz。再生成はPython 3で `py -3.11 scripts/GenerateTitleAudio.py`。標準ライブラリのみ使用し、乱数シードを固定。通常ビルドは生成済みWAVをコピーする。
- 音量設定は編集中もBGM・効果音へ反映する。0%は無音、取消で保存済み音量へ戻す。最大設定時の再生倍率はBGM0.30、効果音0.35。
- BGMは0.4秒で立ち上がり、開始・終了ワイプに合わせて音量が下がる。非アクティブ時は全音を停止し、復帰時はBGMの先頭からフェード再生する。シーン破棄時はAudioSystemの破棄で全音声を解放する。
- 入力の押しっぱなし、登場スキップ、無効操作では効果音を重ねない。音声デバイス・個別ファイルの初期化失敗時はログを残し、画面操作は継続する。
- 回帰検証では5音源のデコード、無音でのループ再生と停止、選択・決定・取消・失敗のイベント、押しっぱなし抑止を確認する。曲調・音量バランスの聴感評価は別途行う。

## 背景の待機演出（段階11、2026-09-30）

- 基準カメラの横方向±0.10、高さ±0.04ワールド単位のゆっくりした移動を追加した。回転・前後位置は固定し、ロゴ・メニュー自体は動かさない。
- 街路内に24粒の小さな暖色光を配置した。12秒周期で上昇・フェードし、粒数は増加しない。既存ParticleRendererで深度テストし、建物に隠れる粒は描かない。UIより先に描く。
- TitleMote.hlslで白1画素から円形の光を作る。AI画像、追加の外部モデル・画像素材は使わない。街並みは引き続き採用済みCC0モデルで構成する。
- 設定の背景演出をOFFにすると、その視点でカメラを停止して粒を非表示にする。ONに戻すと同じ時間から再開する。初期OFFの場合は基準カメラで静止する。
- 設定編集中も即時プレビューする。Esc／Bで取り消すと保存済みON/OFFへ戻る。再起動時は保存済み設定を読む。非アクティブ中と画面遷移中は時間を止める。
- 自動検証ではON/OFF・復帰・不正時間・長時間の位置範囲を確認し、3種類の画面サイズで背景を描画する。実際の動きの見やすさは目視調整の対象。

## UI演出（段階10、2026-09-30）

- 初回の登場は0.65秒。ロゴ、メニュー、案内を少しずつ遅らせて左から滑らかに表示する。選択項目は12基準ピクセルだけ前へ出て0.16秒で戻る。設定画面の選択行にも適用する。
- 登場中の新しい入力は登場スキップにだけ使う。同じ決定の押しっぱなしで開始しない。アプリ実行中にゲームからタイトルへ戻った場合は登場を省略する。再起動時は再び登場する。
- 開始・終了の決定後はピンクと黒のワイプを0.32秒で左から右へ広げる。画面全体を覆ったフレームの次の更新で、一度だけシーン遷移または通常終了を要求する。ワイプ中は追加のメニュー操作を受け付けない。
- 非アクティブ時は時間を進めない。不正・負の経過時間は無視し、1更新の時間加算は最大0.1秒に制限する。
- 既存UIアトラスと白1画素の実行時テクスチャを使い、画像素材は追加していない。ワイプは実画面全体へ描画するため、縦長・横長の余白も覆う。
- 背景演出設定は今後追加する背景の動きが対象で、今回の短いUI操作演出とは独立する。
- 回帰検証にスキップと開始の分離、押しっぱなし抑止、非アクティブ中の停止、遷移通知の順序と一度だけの発行、選択演出の収束を追加した。3種類の画面サイズで登場途中からワイプ完了まで描画する。

## 設定画面（段階9、2026-09-30）

- 「設定」を有効化し、音量（0～100%、10%刻み）、背景演出（ON/OFF）、保存して戻るを追加した。上下で項目、左右で値を変更する。背景演出は決定でも切り替えられる。
- Enter／Aで決定、Esc／Bで変更を取り消してメニューへ戻る。保存失敗時は設定画面にエラーを表示し、再試行できる。決定の押しっぱなしで画面をまたいで操作しない。
- GameSettings が LocalAppData/WP1/settings.txt にバージョン付きの設定を保存する。欠損・形式不正・範囲外・未対応バージョンは既定値（100%、ON）に戻す。一時ファイルへの書き込み後に置換し、置換失敗時は既存ファイルを維持する。
- ゲームシーンの既存 Sample.wav に保存済みの音量倍率を適用する。100%時は従来の音量0.25、0%時は無音。再生操作は既存のSpace／A。タイトル用BGMと操作音は段階12で接続済み。
- 背景演出の設定は段階11で待機カメラと光の粒へ接続済み。編集中もその場でプレビューする。
- UiAtlas.pngを2048×2048に拡張し、同じ同梱フォントで設定ラベル、数値、操作案内、エラー文を生成した。AI画像や追加モデルは使用していない。
- 自動検証は設定の保存・再読み込み・置換・不正ファイル・失敗時の保持・取消・上下限・パッド入力を対象とする。設定画面の3行とエラー表示を3種類の画面サイズで描画する。実コントローラーによる操作確認は別途必要。

## メニュー操作（段階8、2026-09-30）

- キーボードの上下／W・Sと、ゲームパッドの十字キー／左スティックで選択し、Enter／Aで開始・終了する。終了は既存メッセージループのWM_QUIT経由。
- 入力の立ち上がり、接続時・復帰時の抑止、スティックのヒステリシスを実装した。直前に操作した機器に応じて案内を切り替える。
- 段階8では設定を飛ばしていたが、段階9から全3項目を選択できる。

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
