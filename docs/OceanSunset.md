# 海に映る夕焼け — エディター用シーン

Editorの「ファイル → シーンを開く」またはProjectで
`Assets/Scenes/OceanSunset.json` を開き、GameビューにフォーカスしてPlayします。
専用シーンを直接開く場合は `Editor.exe --scene=Assets/Scenes/OceanSunset.json`。
起動設定と既存のタイトル・公園シーンは変更していません。

シーンの「夕景 / 空・雲・海面反射」を選ぶと、Skyの「立体的な海と夕空」で
太陽の高度・方位・視直径、水の屈折率、海面の高さ、波の振幅・周期、雲量・速度を編集できます。
大気のエアロゾル・オゾン密度倍率、雲底高度・厚さ・消散係数・散乱異方性・不透明度、
水中の色も調整できます。雲底高度は海面からの高さです。
「描画品質と形状」で大気・雲の積分回数、波の成分数・探索回数、太陽光の強度と色、波の方向・成分比、雲の空間倍率・風・ノイズ混合比を編集して保存できます。既定の品質・形状は従来と同じです。Scene／Gameビューの描画倍率と連続描画はシーン設定で調整できます。
カメラの位置・向き・画角が海面の見え方と反射に反映されます。
Sceneビューでもエディターのカメラで周囲を見渡せます。
1単位は1m、+Xは東、+Zは北です。

音は専用の36秒・32 kHzステレオ波音のみです。音楽は含めていません。
AudioSourceの音量・有効状態を編集できます。Play中のPauseと停止は既存の音声制御に従います。
波音は合成したオリジナル素材で、現地録音ではありません。

## 現実の値と制作上の選択

| 項目 | 採用した基準 |
| --- | --- |
| 観測地点・日時の参考 | 江の島付近 35.298°N / 139.474°E、2026-10-12 16:58 JST |
| 太陽位置 | NOAAの近似式で高度1.9933°、北から時計回りの方位259.7663° |
| 太陽の視直径 | NASAの平均1919秒角に対応する約0.533° |
| 水の屈折率 | 可視光で約1.333。入射角による反射率をFresnel式で計算 |
| 波の分散関係 | 深水波の ω² = gk、g = 9.80665 m/s² |
| 主波の周期・波長 | 4.5秒 → 約31.61m。振幅0.075mは穏やかな海の制作設定 |

太陽の方位と高度は地理と日時から計算した値です。
波高・雲量・霞・色・音は見た目を整えるための制作設定で、その日の実測気象ではありません。
添付写真は色と静かな反射の参考として扱い、背景画像を貼り付けていません。

## 描画の仕組みと近似範囲

画素ごとにカメラからワールド空間のレイを生成し、波面との交点、空・雲から来る光を計算します。
エンジンの全画面四角形はレイの生成に使い、画像のサンプリングは行いません。
添付画像もコピーしていません。カメラを動かすと視点・水平線・反射が変わり、
海面の交点から`SV_DEPTH`を出力するため、通常のモデルと海面の前後関係も判定します。

大気は半径6360 kmの地球と上端6460 kmの球殻です。Hillaireの地球モデルを参考にした
RGB散乱・吸収係数を使い、Rayleigh密度は高度8 km、Mie密度は1.2 kmを尺度とする指数関数、
オゾン密度は高度25 kmを中心に半幅15 kmの三角形で近似します。
視線16区間・太陽光8区間の単一散乱積分で夕焼けの赤化を計算します。
太陽光のRGB値20は露出を整える制作設定で、測定した放射量や完全なスペクトルではありません。

雲は移動する3Dノイズの密度場です。海面から1000 mの雲底、1300 mの厚さ、
消散係数0.004/mを初期値とし、視線24区間・太陽光4区間でBeer–Lambert則による減衰と
Henyey–Greenstein位相関数による散乱を計算します。雲の分布は実測気象ではありません。

海面は深水波の分散関係に従う9成分の高さ場で、解析的な微分から法線を求めます。
交点は18回までの保守的な進行と最大12回の区間内補間で近似します。
画素より小さな遠方の波で交差計算が収束しない場合は、クリップ範囲内の平均海面へ補完します。
反射方向から同じ空・雲を再評価し、偏光を平均した誘電体のFresnel式で反射率を求めます。
太陽の視直径と画素の大きさで円盤の被覆を近似し、遠方の細かな波を減衰させます。
光はHDRの線形色で合成して露出・トーンマッピング・弱いBloomを適用し、SDR出力にも対応します。

これは有限区間の決定的なリアルタイム積分です。ハードウェアDXRやパストレーサーではなく、
少ない積分点による誤差、とくに水平線近くの雲や波の近似が残ります。
大気・雲の多重散乱、完全な水中散乱、物体の反射、FFTによる波スペクトル、
海流・砕波・海底・波面との衝突は実装していません。
海面は描画用の環境で、MeshやColliderではありません。
既存のシーンは`ocean`設定を持たないため、従来の空の描画を使います。
専用シェーダーもこの設定を使うシーンにだけ準備されます。

## 調査資料

- [NOAA: 太陽位置の計算式](https://gml.noaa.gov/grad/solcalc/solareqns.PDF)
- [NASA: Sun Fact Sheet / 視直径](https://nssdc.gsfc.nasa.gov/planetary/factsheet/sunfact.html)
- [NOAA NDBC: 波の急峻度・周期・波長](https://www.ndbc.noaa.gov/faq/algorithm.shtml)
- [Georgia State University: 水の屈折率](https://hyperphysics.phy-astr.gsu.edu/hbase/geoopt/refr2.html)
- [Georgia State University: Fresnel反射](https://hyperphysics.phy-astr.gsu.edu/hbase/phyopt/reflin.html)
- [NOAA NESDIS: 空と夕焼けの色](https://www.nesdis.noaa.gov/about/k-12-education/atmosphere/why-the-sky-blue)
- [Hillaire 2020: 大気モデルと散乱積分](https://sebh.github.io/publications/egsr2020.pdf)
- [Bruneton: 密度分布・オゾンの地球モデル](https://github.com/ebruneton/precomputed_atmospheric_scattering/blob/master/atmosphere/demo/demo.cc)
- [PBRT 4: 媒質の透過率](https://pbr-book.org/4ed/Volume_Scattering/Transmittance)
- [PBRT 4: 散乱位相関数](https://pbr-book.org/4ed/Volume_Scattering/Phase_Functions)
- [PBRT 4: Fresnel反射](https://pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission)
- [NVIDIA GPU Gems: 波の分散関係と解析的法線](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models)

## 再生成と検証

`python scripts/GenerateOceanSunset.py --overwrite` で専用シーン・波音のみを再生成します。
生成時の日時・定数・資料は `generated/ocean-sunset/authoring.json` に記録します。

```powershell
MSBuild tests/OceanSunsetValidation.vcxproj /p:Configuration=Release /p:Platform=x64
generated/tests/outputs/x64/Release/OceanSunsetValidation/OceanSunsetValidation.exe
```

EditorはDevelopment構成でビルドします。検証プログラムは保存・再読み込み、
旧シーンのJSONと描画の互換性、不正値の拒否、D3D12でのHDR・SDR描画、
時間による変化・停止、Sceneカメラの方向変更、海面下・海面上のモデルとの深度合成、
縦長画面、専用波音の開始を確認し、
`generated/ocean-sunset/previews` に画像を保存します。
