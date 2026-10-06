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
シーンを選び、Open selected sceneで開けます。File → Open sceneからもAssets/Scenes内のシーンを選べます。
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

File → New sceneでAssets/Scenes内の新しい.json名を指定し、空のシーンを作成します。既存名は使えません。
Saveまではファイルを作らず、空のままでも未保存として扱います。Save後にProjectへ反映されます。
シーン切り替え時の未保存確認では、保存して続行・破棄して続行・キャンセルを選べます。保存失敗時は切り替えません。
読み込み成功時は選択とUndo履歴を初期化し、失敗時は現在のシーン・保存先・選択・履歴を保持します。
Street Editorに現在のシーン名を表示し、Save／Reloadはそのシーンを対象にします。未保存の新規シーンではReloadを無効にします。

File → Save as...／Ctrl+Shift+SでAssets/Scenes内に別名保存できます。既存ファイルは保存先を表示して上書き確認します。
保存成功時だけ現在のシーン名・Save／Reloadの保存先・未保存状態を更新し、Projectへ反映します。
キャンセルや保存失敗時は配置・選択・保存先・Undo履歴を維持します。別名保存後もUndo／Redoを利用できます。

HierarchyとSceneでCtrl＋クリックすると選択を追加／解除します。通常クリックは一つを選び、Sceneの空白クリックで全解除します。
HierarchyのShift＋クリックは検索結果の表示順で範囲選択し、Ctrl＋Shiftは範囲を追加します。SceneではShiftも追加／解除です。
複数選択はHierarchy・Inspector・Sceneで共有し、Sceneには全選択の枠を表示します。最後に選んだ対象が黄色、他は青です。
選択変更は配置の変更やUndo操作として記録しません。配置のUndo／Redoには複数選択の状態も保存・復元します。
複数選択中はInspectorのActive positionまたはMoveギズモで全対象を同じ量だけ移動します。
主選択を基準にWorld／LocalとSnapを利用でき、間隔・回転・拡縮・選択状態を維持します。連続ドラッグは一回のUndoで戻せます。
移動先が不正な場合は全対象の移動を拒否します。複数選択の回転・拡縮・複製・削除・Fによるフォーカスは未対応です。

シーンJSONの各オブジェクトは任意のparent（親オブジェクトID）を持てます。省略／空文字はルートで、旧形式のシーンもそのまま読めます。
親は同じシーン内のIDを参照し、存在しない親・自分自身・循環参照は読み込み／保存時に拒否します。Inspectorに親IDを表示します。
この段階の親子関係は構造データで、Transformはワールド座標です。親の移動による子の追従やTransformの継承はまだ行いません。
親を削除した場合は直接の子をルートへ戻し、配置を維持します。複製した対象は元と同じ親を持ちます。

Hierarchyは親子のツリー表示です。矢印で子を開閉し、行を親にしたい対象へドラッグすると親子付けできます。
Drop here to make rootへ落とすと親を解除します。InspectorのParentからも一つずつ親を変更できます。
親子付けはワールドの配置と選択を維持し、Undo／Redo・保存・再読み込みに対応します。自分や子孫への親子付けは拒否します。
検索中は一致する対象を階層によらずフラットに表示し、Shift範囲選択は現在表示中の行に適用します。

シーン保存形式はversion 2です。Position・Rotation（XYZラジアン）・Scaleは常に親に対するローカル座標です。
親がない場合はローカル座標がワールド座標になります。描画・クリック選択・選択枠はlocal × parentWorldで計算し、せん断も保持します。
InspectorのLocal position / Local rotation / Local scaleは保存する値を直接編集します。
ギズモのLocal/Worldは操作軸の指定です。軸は親子の回転から計算し、反転・非一様拡縮・せん断から分離します。
Moveはワールド移動量をローカル位置へ変換し、Rotateはローカル回転だけ、ScaleはLocal軸でローカル拡縮だけを変更します。
親と子の同時移動では子孫を二重に移動しません。

親変更・親解除はワールド配置を維持します。親削除は直下の子だけをルートへ戻し、孫以下の親子関係を保持します。
配置を保存するためにせん断が必要な場合は操作全体を拒否し、対象IDと理由を表示します。
複製は同じ親の下へ一個作成し、ワールド移動量を親座標へ変換して位置だけを変更します。ローカル回転・拡縮は元の値を保持します。

UndoはギズモやInspectorの入力欄ごとの操作IDで区切ります。一回のドラッグ・文字入力を一回のUndoとし、別の入力欄へ移ると直前の操作を確定します。
親変更・Reset・作成・削除は別の操作です。無変更・失敗した操作では変更状態を立てず、Redoと選択を維持します。
保存時は未確定の編集履歴を確定します。失敗したモデル作成では予約したオブジェクトIDを消費しません。

Transformの計算・検証・分解はSceneRuntimeのSceneTransformsへ集約しています。Editor専用の行列実装はありません。
SceneWorldのSetLocalTransform / SetWorldTransform / TranslateObjectsWorldは座標系を明示した編集APIです。
WorldMatrixは描画・選択用、WorldRotationはギズモの回転軸用、LocalTransformFromWorldは親に対するSRTへの変換です。

保存形式のWorld/Local切り替えとEnable parent transformsメニューは廃止しました。
旧version 1シーンは`python scripts/ConvertSceneToLocal.py <scene.json>`で検証し、`--write`を付けて一度だけ変換してください。
通常のエディターはversion 2だけを読み込みます。変換不能な入力は変更せず、version 2を二重に変換しません。

最終の手動確認は既存シーンを編集せず、New sceneで新しい名前のシーンを作って進めてください。

1. Projectからモデルを三個配置し、Parent・Child・Grandchildと命名する。
2. ChildをParentの子、GrandchildをChildの子にし、配置が変わらないことを確認する。
3. Parentを移動・回転・非一様拡縮し、子孫と選択枠が追従することを確認する。
4. ChildでLocal/World回転とLocal拡縮を試し、操作していないInspectorの成分が変わらないことを確認する。
5. ChildとGrandchildをCtrlで同時選択して移動し、二重に動かないこととCtrl+Z一回で戻ることを確認する。Ctrl+Yで復元する。
6. Save/Reloadで配置・親子関係を確認する。
7. Parentの拡縮を均等に戻し、Childの複製とParentの削除を試す。子孫の配置保持、Undo/Redo、Save as/Open後の復元を確認する。

自動の通しテストは新規作成、三段階層、複数選択ドラッグ、Undo/Redo、保存、複製、親削除、別名保存、再読み込みと描画まで確認します。
画面上の入力とギズモの感触は上記の手動確認で確認してください。
