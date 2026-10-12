# こもれび公園の基準ステージと制作時の自動生成

タイトルの START から `Assets/Scenes/ParkAdventure.json` を遊べます。
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
今回のモデルはリポジトリ内のプリミティブと専用パレットによる試作用アートです。
ディスカバリーと同等の完成アート・アニメーション・音響・ステージ密度を達成したものではありません。

## モジュールと生成

`Content/Assets/Prefabs/Park` に5つの編集可能なPrefabを保存しています。
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
python scripts/GenerateParkAdventure.py --seed 7 --output Content/Assets/Scenes/ParkCandidate7.json
```

標準の基準ステージを再生成する場合:

```powershell
python scripts/GenerateParkAdventure.py --seed 20261012 --output Content/Assets/Scenes/ParkAdventure.json --overwrite
```

`--author-library` はモデル・マテリアル・Prefabの初期ライブラリも再作成します。
手で編集したPrefabを置き換えるため、通常の候補生成には指定しません。
ビルド時に生成器は実行せず、保存済みシーンを使用します。
生成の記録と構造検査結果は `generated/park/reports` に保存します。

## ゲーム処理のビルド

`Content/Assets/Scripts/ParkAdventure.cpp` を既存のC++ゲーム処理の自動コンパイル機能で読み込みます。
Editorを起動するとソースの変更が検出され、対応構成のモジュールがコンパイルされます。
Appを直接実行する場合は、Appをビルドした後で、対応するゲーム処理を実行先に出力します。

```powershell
MSBuild App/App.vcxproj /p:Configuration=Development /p:Platform=x64
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/BuildScripts.ps1 -Configuration Development -OutputContent generated/outputs/x64/Development/App
```

配布は既存の `scripts/BuildPlayer.ps1` を使用します。保存済みC++ソースを配布用にコンパイルし、DLLとCRTを同梱します。

## 検証

```powershell
python -m unittest discover -s tests -p test_park_generation.py -v
MSBuild tests/ParkAdventureValidation.vcxproj /p:Configuration=Release /p:Platform=x64
generated/tests/outputs/x64/Release/ParkAdventureValidation/ParkAdventureValidation.exe
```

Python検証は100種類のSeed、再現性、重複ID、親参照、スイッチと扉の順序、中央ルートの足場欠落を確認します。
ジャンプ距離は通常ジャンプの理論距離に安全係数を掛けた構造検査です。動く足場や自由な地形には対応していません。
C++検証は実際のJoltキャラクターとゲーム処理でコースを進み、空中停止の制限、戦闘、12個の収集、落下復帰、扉、クリア、再挑戦を確認します。
通し検証はReleaseを推奨します。Debugも同じ検証を実行できますが、CPUでの再生に時間が掛かります。
描画はSceneEnvironmentを使って検証し、入口と水路の画像を `generated/park/previews` に保存します。

次の拡張では、操作感のプレイテスト、完成用モデルとアニメーション、効果音、場面ごとのカメラ設定、場面の固定・部分再生成を進めます。
現状は制作時の候補生成です。プレイするたびの生成、生成AIによる企画、任意のPrefabを対象とする自動攻略検査は含みません。

## 2026-10-12の検証結果

- Appとゲーム処理DLLのDebug・Development・Releaseを通常のVisual Studio設定でビルドしました。
- Pythonの6テストが成功しました。100個のSeed、再現性、不正なルート・参照・目標の拒否、設定ファイルへの出力拒否を確認しました。
- Releaseの物理通しプレイは1924フレームで完了し、12個の収集、全敵の撃破、ジャンプと空中停止、チェックポイント復帰、2つのスイッチ、クリアと再挑戦の予約を確認しました。
- DebugとReleaseで入口・水路を描画し、水面が岸に隠れない高さへ調整しました。Debugの長い全コース再生は途中までとし、全コースの確認にはReleaseを使用しています。
- 既存のシーン編集・タイトル遷移・プロジェクト設定の回帰検証が成功しました。
- Release配布物を生成し、通常のタイトル起動に加え、公園を一時的な起動対象とする検証でAppとDLLの読み込み・更新・描画を確認しました。配布物はタイトル起動へ戻しています。

描画検証は自動再生とGPU画像による確認です。人間による操作感・楽しさの評価は今後の工程です。
