# 街の配置エディター

`WP1.slnx` の Debug または Development をビルドし、
`generated/outputs/x64/<構成>/Editor/Editor.exe` を起動します。
Release のソリューションビルドはゲームを対象とし、Editor は除外します。

- 右ドラッグ：視点回転
- 右ドラッグ＋WASD：移動、Q/E：上下移動、Shift：加速
- R／Reset camera：初期視点へ戻る
- F／Focus selected：選択モデル全体が画面に収まる位置へ移動（向きは維持）
- Ctrl+S：保存、Ctrl+D：選択対象を複製、Delete：選択対象を削除
- 1／2／3：ギズモを移動／回転／拡縮に切替

起動時は上部にFile／Edit／Viewメニューとツールバー、左に状態表示とHierarchy、中央にScene、右にInspectorを配置します。
中央のScene／Gameはタブで切り替え、分割して並べることもできます。View → Game tabでGameを選択します。
Gameは現在の配置（未保存を含む）をタイトルの固定カメラ・画角・照明で表示します。空・光の粒・タイトルUIは含みません。
Game上では選択・ギズモ・モデル追加は行わず、Sceneの視点を維持します。既存の全画面構図プレビューも利用できます。
保存済みのレイアウトでGameが中央にない場合は、View → Reset panel layoutで標準配置へ戻せます。
下部のProject／Console／Debug Cameraはタブで切り替えます。パネルはドッキング・分割・移動・サイズ変更できます。
View → Reset panel layoutで標準配置へ戻します。配置はLocalAppData/WP1/Editor/layout.iniへ保存し、再起動時に復元します。
ツールバーにはSave・Undo・Redo・Move／Rotate／Scale・World／Local・Snapを表示します。
Snap settingsで刻みを変更できます。Scaleは常にLocalで、座標系ボタンは無効です。
FileにはSave・Reload・Exit、EditにはUndo・Redo・Duplicate・Delete、ViewにはFocusと構図プレビューがあります。
状態表示パネルには未保存状態・保存結果・レイアウト保存エラーを表示します。
ショートカットは文字・数値入力中、右ドラッグ、ギズモ操作、確認ダイアログ中は無効です。

Hierarchyで遠くのモデルを選んでFを押すと、そのモデルへカメラを寄せられます。
画面の縦横比とモデルの大きさに合わせて距離を調整します。配置やUndo履歴は変わりません。
数値入力・ギズモ操作・右ドラッグ・確認ダイアログの表示中とプレビュー中は使いません。

Hierarchyで名前・ID・モデル名を検索して対象を選び、Inspectorで位置・回転・大きさを編集できます。
数値はドラッグ、またはCtrl＋クリックで直接入力できます。回転の表示単位は度、JSONはラジアンです。
変更は即座に描画へ反映されます。SaveでJSONへ保存、Reloadでディスクから再読み込みします。
未保存でReloadを押すと、保存して読み込み・破棄して読み込み・キャンセルを選べます。
読み込みや保存が失敗した場合は、今の配置と未保存状態を維持してエラーを表示します。
未保存でウィンドウの×やAlt+F4から終了すると、Save and exit（保存して終了）、
Exit without saving（保存せず終了）、Cancelを選べます。保存失敗時は終了せずエラーを表示します。
プレビュー中も同じ確認を表示し、保存済みならそのまま終了します。
ゲームには次のビルドで保存したJSONがコピーされます。

Preview title compositionで編集パネル・ギズモ・選択枠を隠し、ゲームと共有する
タイトルの固定カメラ・画角・照明で現在の街を確認できます。未保存の編集も反映します。
Back to editingまたはEscapeで元の編集視点へ戻ります。プレビュー中は配置を編集しません。
これは街の構図確認用で、ゲームの空・光の粒・カメラの揺れ・タイトルUIは含みません。

Projectの左側でフォルダーを選ぶと、そのフォルダー直下のモデル（.obj）とシーン（Assets/Scenes内の.json）を表示します。
Search assetsは全フォルダーの相対パスを検索し、TypeでAll／Models／Scenesを切り替えます。
Refreshでファイルを再走査します。失敗時は以前の一覧と選択を維持し、エラーを表示します。
モデルを選び、Add selected modelで追加できます。Projectのアセット選択はHierarchyのオブジェクト選択とは独立しています。
シーンはこの段階では選択とパス表示に対応し、現在の編集シーンは切り替えません。
追加位置は数値入力、またはUse camera frontでカメラの約8単位先の地面へ合わせます。
Projectのモデル行をSceneの画像へドラッグ＆ドロップすると、カーソルの視線と地面（Y=0.08）の交点へ追加します。
空や水平線など地面に届かない場所では視線の約8単位先に配置します。シーンアセットはドラッグ対象外です。
ドロップは一回の追加操作としてUndo／Redoに対応し、画像外や確認ダイアログ中では追加しません。
新しいモデルの拡縮は4です。追加後は自動選択され、Inspectorで調整できます。
InspectorのDuplicateはX方向へ4単位ずらして複製し、Deleteは選択対象を削除します。
追加・複製・削除の結果もSaveでJSONに保存されます。
Undo／Redoボタン、Ctrl+Z／Ctrl+Y（またはCtrl+Shift+Z）で直近100操作を戻せます。
Inspectorとギズモの連続ドラッグは一操作にまとめます。追加・複製・削除も対象です。
保存後も履歴は残り、保存済みの配置に戻ると未保存表示が消えます。Reload成功時は履歴を消去します。

街のモデルを左クリックすると、手前の三角形に当たった対象を選択します。
空をクリックすると選択を解除します。パネル上のクリックは街の選択に使いません。
選択対象には黄色の立体枠を表示し、HierarchyとInspectorも同じ対象を表示します。
選択枠は確認用の重ね表示のため、他のモデルに隠れた部分も表示します。

上部ツールバーでMove／Rotate／Scaleを選び、選択対象の色付きハンドルを左ドラッグします。
ImGuizmoの移動・回転ハンドルはWorld／Localを切り替えられます。拡縮はローカル軸です。
Snapを有効にすると、移動4単位・回転15度・拡縮0.25刻みにそろいます。刻みはSnap settingsで変更できます。
ギズモ操作中は自由カメラ、追加・複製・削除、保存・再読み込みを止めます。
操作結果はInspectorに反映され、Saveで保存できます。ImGuizmoはEditorだけに組み込み、MITライセンスを同梱します。

## 依存関係

```text
App ──────→ SceneRuntime ──→ Engine
Editor ───→ SceneRuntime ──→ Engine
```

SceneRuntime は配置データとモデルの描画を担当し、ゲーム入力や編集UIを含みません。
App はタイトルのUI・空・光の粒・待機演出を担当します。
Editor は独自の起動処理・自由カメラ・編集UIを担当し、Appを参照しません。

モデル・JSON・共有のメッシュシェーダーは `Content` にあります。
Appへの配置は `Content/Content.targets` が行います。
開発環境のEditorは実行ファイルから上位へ探索して、元の `Content` を読みます。
元のContentが見つからない配布環境では、実行ファイルに同梱されたデータを読みます。
このため開発時に扱うJSONは、ビルド出力にあるコピーではなく
`Content/Assets/Scenes/TitleStreet.json` です。

## 編集状態

選択ID・未保存表示・変形の検証と適用・追加／複製／削除の要求は、ImGuiに依存しない
`EditState`で共有します。ObjectPanelは表示と入力、SceneSelectionはクリック選択、
TransformGizmoはハンドル操作を担当し、同じ編集状態を参照します。
GPUの待機が必要な操作とUndo／Redo・保存はStreetEditorが実行します。

## Scene表示領域

`SceneViewport`で表示領域の位置・サイズ・縦横比と画面座標変換を共有します。
カメラ投影、クリック選択、選択枠、ギズモ、右ドラッグ開始はこの領域を使います。
Sceneパネルの画像領域を描画・入力の対象とし、タイトルバーや他のパネル上ではクリック選択・カメラ移動を開始しません。
サイズが無効な間は選択・ギズモ・カメラ移動を止め、最後の有効な投影を維持します。

## オフスクリーン描画先

Engineの`RenderTexture`はRGBA8の色テクスチャ・深度バッファー・RTV・コピー用SRVを所有します。
`Resize(renderer, width, height)`はRenderの外で呼び、GPU完了後に描画先を置き換えます。
同じサイズなら再生成せず、サイズ0・不正サイズ・生成失敗なら以前の描画先を保持します。
Renderの描画コールバック内でBegin→SceneWorldのDraw→Endを呼ぶと、色テクスチャを
シェーダーから読める状態に戻します。End後は次の描画先を呼び出し側で設定します。
SRVはUIのディスクリプターヒープへコピーして使います。破棄前にはWaitForIdleが必要です。
EditorはこのテクスチャをSceneパネルに表示します。サイズ変更は次のRender前に反映し、UIのSRV枠を再利用します。

InspectorのNameで名前を編集できます。IDとモデルは変わらず、名前・ID・モデル名の検索へ即時反映します。
空欄・空白だけの名前は拒否し、入力を終えると最後の有効な名前へ戻します。連続入力は一回のUndoにまとめます。
Transformは折りたたみ可能で、Reset Transformは位置・回転を0、拡縮を1へまとめて戻します。
名前変更とリセットはUndo／Redo・保存・再読み込みに対応します。Hierarchyはこの段階では平坦な一覧です。

Consoleはエンジンのログと保存・読み込み・追加・複製・削除・Undo／Redoの結果を直近500件表示します。
Debug／Info／Warning／Errorの種類別フィルター、Search logs、Clearに対応します。検索は英字の大文字・小文字を区別しません。
Auto-scrollは末尾を表示している場合のみ新しいログへ追従します。Clearは画面用の履歴だけを消し、診断ログファイルは残します。
View → Consoleでタブを選択できます。既存の保存レイアウトでは必要に応じてReset panel layoutを使ってください。
