# 公園サンプルの操作・生成・検証

この文書のゲーム素材は、WP1 の外にある `../GuruttoWorld` で管理します。制作スクリプトの `scripts/...` はゲーム側ルートで実行し、エンジン側の C++ 検証コマンドは WP1 ルートで実行してください。

公園の基準シーンは `Assets/Scenes/ParkAdventure.json` です。ゲームの起動先は `Assets/Project.json` で確認してください。
Editor ではこのシーンを開き、Game にフォーカスを置いて Play します。
移動は WASD・矢印・左スティック、ジャンプは Space・A、攻撃は J・X、再挑戦は R・Start、タイトルへ戻る操作は Esc・B です。

ジャンプ後にボタンを押し続けると、上昇開始から0.25秒後に最大0.85秒の空中停止が使えます。
空中停止はホバリングの操作を試すための暫定仕様です。上昇する飛行やコピー能力は実装していません。
敵への攻撃は近距離の範囲攻撃です。敵や収集物との間に壁がある場合、Raycastで判定して攻撃・収集を防ぎます。
敵に触れるとライフが減り、一定時間は連続ダメージを受けません。落下やライフ切れで最後のチェックポイントへ戻ります。
収集物とスイッチの進行は復帰時に保持され、再挑戦でリセットされます。

ステージは導入、段差への寄り道、水路のジャンプ、左右のスイッチ、展望台の順です。
花のかけら12個は任意の収集目標で、出口は2つのスイッチを踏むと開きます。
奥のランドマークを示す自動カメラ、攻撃の白いエフェクト、進行に応じた操作説明とクリア表示を含みます。
モデルはリポジトリ内のプリミティブと専用パレットによる試作用アートです。
ディスカバリーと同等の完成アート・アニメーション・音響・ステージ密度を達成したものではありません。

## モジュールと生成

`../GuruttoWorld/Assets/Prefabs/Park` に5つの編集可能なPrefabを保存しています。
生成器はこれらを読み、モジュールのルートをワールド座標へ焼き込んだ通常のシーンを出力します。
キャラクターの目・足などの親子関係は維持します。生成した配置とPrefabはライブ接続されません。
Prefabを編集してから生成すると、その内容を使って次の候補を作れます。

```powershell
python scripts/GenerateParkAdventure.py --seed 7 --output generated/park/Candidate7.json
```

同じSeedと同じPrefabから同じ候補が作られます。Seedは段差エリアの左右反転と木・花の配置を変えます。
主ルートと仕掛けの順序は固定し、まず基準ステージの品質を保持する範囲に限定しています。
既存ファイルは `--overwrite` を指定した場合だけ置換します。
候補をEditorで再生する場合は、次のようにAssets/Scenesへ直接出力してください。
出力するシーン内の再挑戦先も、そのシーン自身に設定されます。

```powershell
python scripts/GenerateParkAdventure.py --seed 7 --output Assets/Scenes/ParkCandidate7.json
```

標準の基準ステージを再生成する場合:

```powershell
python scripts/GenerateParkAdventure.py --seed 20261012 --output Assets/Scenes/ParkAdventure.json --overwrite
```

`--author-library` はモデル・マテリアル・Prefabの初期ライブラリも再作成します。
手で編集したPrefabを置き換えるため、通常の候補生成には指定しません。
ビルド時に生成器は実行せず、保存済みシーンを使用します。
生成の記録と構造検査結果は `generated/park/reports` に保存します。

## ゲーム処理のビルド

`../GuruttoWorld/Assets/Scripts/ParkAdventure.cpp` を既存のC++ゲーム処理の自動コンパイル機能で読み込みます。
Editorを起動するとソースの変更が検出され、対応構成のモジュールがコンパイルされます。
プレイヤーとEditorのビルドでは、Contentの配置後に同じ構成のゲーム処理DLLを自動コンパイルして実行先へ出力します。エンジンの公開ヘッダーを変更した場合も、本体とDLLのABIが揃うため、別途DLLを手動配置する必要はありません。

```powershell
# GuruttoWorld ルートで実行します。
./Build.ps1 -Configuration Development
```

配布は WP1 側のC++製 `ProjectTool.exe` を使用します。WP1をDevelopment・x64でビルドし、WP1ルートで `& ./generated/outputs/x64/Development/ProjectTool/ProjectTool.exe build-player --project ../GuruttoWorld --configuration Release` を実行します。保存済みC++ソースを配布用にコンパイルし、DLLとCRTを同梱します。

## 検証

```powershell
# GuruttoWorld ルートで実行します。
python -m unittest discover -s tests -p test_park_generation.py -v
```

```powershell
# WP1 ルートで実行します。
MSBuild tests/ParkAdventureValidation.vcxproj /p:Configuration=Release /p:Platform=x64
generated/tests/outputs/x64/Release/ParkAdventureValidation/ParkAdventureValidation.exe
```

Python検証は100種類のSeed、再現性、重複ID、親参照、スイッチと扉の順序、中央ルートの足場欠落を確認します。
ジャンプ距離は通常ジャンプの理論距離に安全係数を掛けた構造検査です。動く足場や自由な地形には対応していません。
C++検証は実際のJoltキャラクターとゲーム処理でコースを進み、空中停止の制限、戦闘、12個の収集、落下復帰、扉、クリア、再挑戦を確認します。
通し検証はReleaseを推奨します。Debugも同じ検証を実行できますが、CPUでの再生に時間が掛かります。
描画はSceneEnvironmentを使って検証し、入口と水路の画像を `generated/park/previews` に保存します。

現状は制作時の候補生成です。プレイするたびの生成、生成AIによる企画、任意のPrefabを対象とする自動攻略検査は含みません。

## 性能計測

配布プレイヤーの本番ループは `App.exe --profile-park --profile-frames=600 --profile-output=C:/temp/park.csv` で計測できます。
タイトルの計測には `--profile-title` を使います。入力を取得できない検証環境だけで `--profile-simulate-inactive` を指定し、実操作の結果とは分けて扱います。
CSV の時間は包含区間を含み、GPU 計測は完了済みフレームの値です。区間の意味と制約は[性能計測](Profiling.md)、回帰検証の実行方法は[テスト手順](../tests/README.md)を参照してください。
