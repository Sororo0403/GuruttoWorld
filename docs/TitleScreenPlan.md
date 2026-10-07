# タイトル画面の制作仕様

更新日：2026-10-07

## 現在の構成

「ぐるっとワールド」の朝のCC0街並み、大きな斜めロゴを維持する。
開始案内をSTART／CONFIGの2項目へ変更し、人物は配置しない。
上下キー・W/S・パッドで選択、Enter／Aで決定。ポインターの入場で選択し、クリックで決定する。
押しっぱなし、フォーカス復帰、パッド接続直後の誤操作を抑止する。

## カメラと設定

登場カメラは約1.3秒。STARTは通りを見渡す構図、CONFIGは歯車付きの施設へ移動する。
CONFIGへの移動は0.62秒のoutBack、STARTへの復帰は0.56秒のoutBack。
選択変更時の現在位置・回転から補間するため、途中で反転してもカメラが飛ばない。
移動先と補間はメインカメラのAnimation Componentに保存し、configFocusTime／homeFocusTimeで区別する。
待機の揺れはmotionTime、登場はsceneTime、ゲーム開始はstartTimeを使う。
開始は0.8秒でカメラを奥へ動かし、オレンジ・紺の帯で全画面を覆う。

CONFIGを決定すると音量・背景演出・保存して戻るを表示する。
左右で設定変更、Esc／Bで取消。設定保存失敗時は設定を開いたままエラーを表示する。
設定中もCONFIGのカメラ位置を保持し、ロゴは小さく右上へ配置して歯車を隠さない。
背景演出OFFは待機の揺れと粒を停止する。メニュー選択による移動は操作のフィードバックとして動く。

## 街・光・影

建物は既存Kenney City Kit (Commercial)、歯車はKenney Factory Kit 3.0のcog-a.objを使用する。
歯車のパレットをオレンジへ変更。CC0のライセンスと取得元をTitleScreenAssets.mdに記録する。
環境光を抑え、暖かい直射光と青い遠景の霞で手前・奥の差をつける。
建物・高層建築・街灯・歯車の形状を光の方向に投影し、道路・歩道のテクスチャへ影を焼き込む。
地面の元のUV模様・白線と個別配置を維持し、影の重なりで二重に暗くしない。
この影は固定タイトル用のベイク影。建物・地面・光の方向を編集したら再ベイクが必要。

## 編集・再生成

通常はContent/Assets/Scenes/TitleStreet.jsonをEditorで編集する。通常ビルドはアセットを再生成しない。
scripts/AuthorTitleMenu.pyはメニュー・施設・初期カメラ移動を明示的に再設定する制作ツール。
scripts/GenerateMorningTitleArt.ps1は同梱M PLUS 1p BlackからロゴとSTART／CONFIGの帯を生成する。
scripts/BakeTitleGroundShadows.pyはNumPy／Pillowで固定影を再ベイクする。
Shadows/GroundSources.jsonに元モデル参照を保持する。再ベイクは現在の各地面配置を使用する。
旧CreateMorningTitleScene.py／AuthorTitleAnimation.pyは初期制作の記録で、現行メニューを上書きするため通常は実行しない。

## 検証とコミット

メニューと移動基盤、街・影・見た目、最終の演出検証を機能ごとにコミットする。
Debug／Development／ReleaseのApp・共有コードをビルドし、Debug／Releaseの回帰検証を実行する。
TitlePresentationValidationでCONFIG到着・途中反転・START復帰と3画面比率の開始UI・全画面被覆を確認する。
GPU画像はgenerated/title-rebuild/previewsに保存し、Gitには含めない。
非表示GPU検証と実機の画面操作は区別して報告する。
