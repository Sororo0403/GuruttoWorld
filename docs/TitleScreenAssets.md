# タイトル素材の出典・ライセンスと再生成

この文書のゲーム素材は、WP1 の外にある `../GuruttoWorld` で管理します。制作スクリプトの `scripts/...` はゲーム側ルートで実行し、エンジン側の C++ 検証コマンドは WP1 ルートで実行してください。

配置・カメラ・演出の正本は、保存済みの [TitleStreet.json](../../GuruttoWorld/Assets/Scenes/TitleStreet.json) です。通常の編集はEditorで行い、ビルド時に制作スクリプトを実行して上書きしません。

## 元素材とライセンス

| 元素材 | 用途・保存場所 | 同梱ライセンス |
|---|---|---|
| [Kenney City Kit Commercial 2.1](https://kenney.nl/assets/city-kit-commercial) | 建物。元のOBJ・MTL・パレットはCommercialに保存 | [Commercial/License.txt](../../GuruttoWorld/Assets/Models/Title/Commercial/License.txt) |
| [Kenney City Kit Roads 2.1](https://kenney.nl/assets/city-kit-roads) | 道路・橋・街灯・看板。元のOBJ・MTL・パレットはRoadsに保存 | [Roads/License.txt](../../GuruttoWorld/Assets/Models/Title/Roads/License.txt) |
| [Kenney Factory Kit 3.0](https://kenney.nl/assets/factory-kit) | 設定施設の歯車。配色を変更した派生素材 | [Factory/License.txt](../../GuruttoWorld/Assets/Models/Title/Factory/License.txt) |
| [Openclipart「security」by yves_guillou](https://openclipart.org/detail/92155/security-by-yves_guillou-92155) | 終了ゲートの看板。元SVG・PNGとEXIT文字を加えた派生素材をEmergencyExitに保存 | [EmergencyExit/License.txt](../../GuruttoWorld/Assets/Models/Title/EmergencyExit/License.txt) |

各素材のライセンス原文と出典は上記の同梱ファイルを保持します。Openclipartの原SVGは [Source.svg](../../GuruttoWorld/Assets/Models/Title/EmergencyExit/Source.svg) です。

取得元アーカイブと記録済みのSHA-256:

- [Commercial](https://kenney.nl/media/pages/assets/city-kit-commercial/a742d900eb-1753115042/kenney_city-kit-commercial_2.1.zip): `F8B09B081C2BB88BCC126E2DEC1CB40FD0DAD7E7E591B6C26AAEFE96FB35276B`
- [Roads](https://kenney.nl/media/pages/assets/city-kit-roads/74288c9459-1787042796/kenney_city-kit-roads.zip): `22058AF3D68173A7CF9BDA9F0E243A8CEF6BD68168C302EBC76327063849674E`
- [Factory](https://kenney.nl/media/pages/assets/factory-kit/edaac9d4f6-1777639602/kenney_factory-kit_3.0.zip)

## 派生素材の再生成

- Surface以下のモデル・マテリアル・画像は `scripts/GenerateTitleMaterials.py` で再生成します。元のKenneyモデルを保持し、Python 3とPillowを使用します。`--check` は全22ファイルの内容一致だけを検査します。
- Surfaceの `plaster.png`・`asphalt.png`・`concrete.png`・`paving.png` は、本プロジェクトで生成する256×256の反復画像です。元モデルのCC0表記と、独自生成画像の出典は区別します。
- 終了看板は `scripts/AuthorEmergencyExit.py`、タイトルのロゴは `scripts/GenerateMorningTitleArt.ps1` で明示的に生成します。
- ロゴに使うM PLUS 1p Blackは [ゲーム側の制作用フォント](../../GuruttoWorld/authoring/Fonts/MPlus1p/MPLUS1p-Black.ttf) と [OFL.txt](../../GuruttoWorld/authoring/Fonts/MPlus1p/OFL.txt) を使用します。配布元は [Google Fonts](https://github.com/google/fonts/tree/main/ofl/mplus1p)、記録済みTTFのSHA-256は `815821A62CE085E453AF318CA004768E336329D5A7D6F7BED272E97E7862D43E` です。
- タイトル音源は `scripts/GenerateTitleAudio.py` で生成します。Bgm・Select・Confirm・Back・Errorの5音源は、Python標準ライブラリで合成し、外部のサンプル音源は使用しません。

再生成は保存済み素材を書き換えるため、制作時に必要な場合だけ実行します。手作業で編集したシーンや素材を、通常ビルドで再生成しません。
