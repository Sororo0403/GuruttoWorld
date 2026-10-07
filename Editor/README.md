# 街の配置エディター

## ゲームシーンの編集

`Assets/Scenes/Game.json`が通常のゲーム開始先です。モデル・Transform・Rotator・Camera・照明・粒子・Image・音源をエディターで編集して保存できます。従来のエンジン機能デモは`EngineDemo`として残しています。
タイトルのSTARTボタンのTargetで開始先のJSONを変更します。Game eventを`menu:0`にするとAppでは開始演出後にTargetへ遷移し、EditorではButtonの`loadScene`動作で同じシーンを開きます。
MeshRendererの表示条件には`model=0`のような状態条件を設定できます。Canvasの初期状態とButtonの状態値設定でモデルの表示を切り替えられます。非表示モデルは影も描画しません。
ButtonのショートカットはSpace・Escape・1・2から選択します。無効・非表示のボタンは反応しません。AppではパッドAもSpaceに対応します。Editorでは再生中にGameへカーソルを置いて操作します。

タイトルCanvasの「タイトル演出の設定」で登場・入力待ち時間、開始遷移、選択演出の時間と移動量、非選択項目の不透明度、BGMフェード時間、遷移帯の進行倍率を編集できます。数値はCanvasの初期状態として保存され、Undo／Redoに対応します。登場中のクリックは演出のスキップだけを行い、改めて押すと決定します。
Appのメニュー順はHierarchyのオブジェクト順に従います。ButtonのGame event `menu:0`／`menu:1`／`menu:2`が開始／設定／終了の役割を表し、オブジェクトIDを変えても役割は維持されます。無効または初期メニューで非表示の項目はキーボード・パッドの選択対象から外れます。同じ役割の複製は一項目として扱います。設定値の保存などのゲーム処理はAppが担当します。

ファイル → プロジェクト設定で、Appの起動シーン・ウィンドウタイトル・幅・高さを編集できます。保存先は`Content/Assets/Project.json`で、次回のビルドでAppへコピーされます。保存は現在のシーンの保存と独立しています。存在しない起動シーン、不正なサイズ、保存失敗はエラーを表示し、以前の設定ファイルを保持します。


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
編集中のGameは現在のシーン（未保存を含む）のCamera・照明・Sky・ParticleEmitterで表示します。Play中はRotator・CameraSway・粒子・雲の移動を更新し、Sceneにも同じ実行用シーンを表示します。タイトルUIは含みません。
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

Preview Game compositionで編集パネル・ギズモ・選択枠を隠し、シーンのCamera・照明・Sky・粒子で構図を確認できます。未保存の編集も反映します。
Back to editingまたはEscapeで元の編集視点へ戻ります。プレビュー中は配置を編集しません。
編集中は位相0の静止プレビューです。動きを確認する場合はPlayを使用します。

Projectの左側でフォルダーを選ぶと、そのフォルダー直下のモデル（.obj）とシーン（Assets/Scenes内の.json）を表示します。
Search assetsは全フォルダーの相対パスを検索し、TypeでModel／Scene／Texture／Audio／Shader／Fontを絞り込みます。
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
SceneRuntime はシーンのComponentからカメラ・照明・空・粒子・揺れを描画します。App は開始シーンの選択とタイトルのUI・音声を担当します。
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
移動先が不正な場合は全対象の移動を拒否します。複数選択の回転・拡縮・複製・削除・Fによるフォーカスにも対応しています。詳細と手動確認は末尾を参照してください。

シーンJSONの各オブジェクトは任意のparent（親オブジェクトID）を持てます。省略／空文字はルートです。通常の読み込みはversion 2／3／4に対応し、旧version 1は下記の変換スクリプトを使用します。
親は同じシーン内のIDを参照し、存在しない親・自分自身・循環参照は読み込み／保存時に拒否します。Inspectorに親IDを表示します。
Transformは親に対するローカル座標で、親の移動・回転・拡縮に子孫が追従します。
親を削除した場合は直接の子をルートへ戻し、配置を維持します。複製した対象は元と同じ親を持ちます。

Hierarchyは親子のツリー表示です。矢印で子を開閉し、行を親にしたい対象へドラッグすると親子付けできます。
Drop here to make rootへ落とすと親を解除します。InspectorのParentからも一つずつ親を変更できます。
親子付けはワールドの配置と選択を維持し、Undo／Redo・保存・再読み込みに対応します。自分や子孫への親子付けは拒否します。
検索中は一致する対象を階層によらずフラットに表示し、Shift範囲選択は現在表示中の行に適用します。

シーン保存形式はversion 4です。Position・Rotation（XYZラジアン）・Scaleは常に親に対するローカル座標です。
親がない場合はローカル座標がワールド座標になります。描画・クリック選択・選択枠はlocal × parentWorldで計算し、せん断も保持します。
InspectorのLocal position / Local rotation / Local scaleは保存する値を直接編集します。
ギズモのLocal/Worldは操作軸の指定です。軸は親子の回転から計算し、反転・非一様拡縮・せん断から分離します。
Moveはワールド移動量をローカル位置へ変換します。単体選択のRotateはローカル回転だけ、ScaleはLocal軸でローカル拡縮だけを変更します。複数選択では下記のとおり主選択を中心に配置も変わります。
親と子の同時移動では子孫を二重に移動しません。

親変更・親解除はワールド配置を維持します。親削除は直下の子だけをルートへ戻し、孫以下の親子関係を保持します。
配置を保存するためにせん断が必要な場合は操作全体を拒否し、対象IDと理由を表示します。
単体選択の複製は同じ親の下へ一個作成し、ワールド移動量を親座標へ変換して位置だけを変更します。ローカル回転・拡縮は元の値を保持します。

UndoはギズモやInspectorの入力欄ごとの操作IDで区切ります。一回のドラッグ・文字入力を一回のUndoとし、別の入力欄へ移ると直前の操作を確定します。
親変更・Reset・作成・削除は別の操作です。無変更・失敗した操作では変更状態を立てず、Redoと選択を維持します。
保存時は未確定の編集履歴を確定します。失敗したモデル作成では予約したオブジェクトIDを消費しません。

Transformの計算・検証・分解はSceneRuntimeのSceneTransformsへ集約しています。Editor専用の行列実装はありません。
SceneWorldのSetLocalTransform / SetWorldTransform / TranslateObjectsWorldは座標系を明示した編集APIです。
WorldMatrixは描画・選択用、WorldRotationはギズモの回転軸用、LocalTransformFromWorldは親に対するSRTへの変換です。

保存形式のWorld/Local切り替えとEnable parent transformsメニューは廃止しました。
旧version 1シーンは`python scripts/ConvertSceneToLocal.py <scene.json>`で検証し、`--write`を付けて一度だけ変換してください。
エディターはversion 2を読み込み時にComponent形式へ変換し、version 4で保存します。変換不能な入力は変更せず、version 2／3／4を二重に座標変換しません。

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

FまたはView → Focus selectedは、現在選択している全オブジェクトのワールド境界をまとめて画面へ収めます。
カメラの向き、選択、シーン内容は変更しません。カメラの描画範囲へ収まらない場合や選択対象が無効な場合は、カメラを動かさず理由を表示します。
手動確認: 離れた二つのモデルをCtrlで選択してFを押し、両方と選択枠がSceneへ収まることを確認してください。

複数選択の削除はDeleteキー、Edit → Delete、InspectorのDelete selectedから行えます。操作を要求した時点の選択IDをまとめて削除します。
選択されていない子孫は削除しません。直接の親が削除された対象は、ワールド配置を保ってルートへ戻します。
選択された親子は重複削除せず、削除する対象のせん断は分解しません。残す対象の配置がSRTで保存できない場合は削除全体を拒否します。
Undo一回で対象・親子関係・複数選択を復元し、失敗時はRedoと変更状態を維持します。
手動確認: 親と子をCtrlで選択してDelete → 選択していない孫が同じ位置に残ることを確認 → Ctrl+Z/Ctrl+Yで全体を復元・再削除してください。

複数選択の複製はCtrl+D、Edit → Duplicate、InspectorのDuplicate selectedから行えます。選択対象だけを一度ずつ複製し、未選択の子孫は複製しません。
選択した親子はコピー同士の親子関係を保ち、子だけを選んだ場合は元の親を維持します。各コピーのワールド配置をX方向へ4だけずらし、親子へ二重に適用しません。
複製後はコピー全体を選択し、元の主選択に対応するコピーを主選択にします。失敗時はシーン・選択・ID採番を維持し、Undo/Redo一回で複製全体を戻せます。
手動確認: 親と子をCtrlで選択してCtrl+D → コピー同士が親子になり一緒に横へずれる → Ctrl+Z/Ctrl+Yで複製と選択が復元されることを確認してください。

複数選択の回転はSceneで2キーまたはRotateを選び、ギズモで行います。主選択のワールド位置を中心に配置と向きをまとめて回し、Local/World軸と角度スナップを使用できます。
選択した親の子孫は親に追従し、選択した子へ二重に適用しません。非一様スケールの親などで結果がローカルSRTへ保存できない場合は、選択全体を変更せず拒否します。
回転ドラッグはUndo/Redo一回で戻せます。複数選択のScaleにも対応しています。
手動確認: 離れた二つをCtrl選択 → 2キー → 主選択を中心に回転 → Local/WorldとSnapを確認 → Ctrl+Z/Ctrl+Yで一括復元。親子の同時選択でも二重に回らないことを確認してください。

複数選択の拡縮はSceneで3キーまたはScaleを選び、主選択のLocal軸のギズモで行います。主選択の位置を中心に配置と大きさを変え、選択した親の子孫へ二重に適用しません。
一様拡縮は全体の大きさと間隔を変えます。軸ごとの拡縮がローカルSRTに保存できないせん断を生む場合は、選択全体を変更せず拒否します。正の倍率を使用し、既存の負スケールは維持します。
ScaleスナップとUndo/Redoに対応しています。手動確認: 離れた二つをCtrl選択 → 3キー → 中央の一様拡縮ハンドルで間隔と大きさが一緒に変わる → Ctrl+Z/Ctrl+Yで復元。親子の同時選択でも二重に拡縮されないことを確認してください。

Play/Pause/Stopの第一段階として、ツールバーに実行状態の操作を追加しました。PlayでPlaying、PauseでPaused、ResumeでPlayingへ戻り、StopでEditingへ戻します。
PlayingでだけGameの時計を進め、Paused・ウィンドウ非アクティブ・終了確認中は進めません。Resumeは時計を維持し、Stopは時計をリセットします。
Playing/Paused中は配置・選択の編集、保存、シーン切り替え、Undo/Redoを無効にします。Sceneのカメラ確認は引き続き行えます。Playは編集中の操作・ダイアログ・保留中の変更が終わるまで無効です。
Gameの実行は下記の共通ランタイムへ接続しています。Pause中はStepで1/60秒ずつコマ送りします。
手動確認: 未保存の編集と複数選択を作る → PlayでGameへ切り替わり時計が進む → 編集・Save・Undoが無効 → Pauseで時計が止まる → Resumeで続く → Stopで時計が0になり編集・選択・Undo履歴・未保存状態が維持されることを確認してください。

Playで現在の未保存シーンを実行用シーンへコピーし、そのComponentだけを更新・描画します。背景の更新処理はAppのタイトルとSceneRuntimeで共有しています。タイトルUI・メニュー・音声は実行対象に含みません。
PauseはGameの時計・カメラ・光の粒を止めて描画だけを続け、Resumeは同じ実行用シーンを継続します。StopはGPU完了後に実行用シーンを破棄し、編集時の静的なGameプレビューへ戻します。
実行開始・終了は描画の外で処理します。開始失敗時はEditingを維持してConsoleへ理由を表示し、編集配置・選択・保存済みシーンを変更しません。
手動確認: モデルを移動して未保存のままPlay → Gameにその配置と空・動く光の粒が出る → Pauseで粒と視点が止まる → Resumeで続く → Stopで静的プレビューへ戻り未保存の編集が残ることを確認してください。

Pause中だけStepボタンを使用できます。一回につき1/60秒の更新を一度だけ実行し、Pausedを維持します。描画や次の通常フレームでは追加更新しません。
手動確認: Play → Pause → Stepを数回押す → 光の粒と時計が一回ずつ進み、押していない間は止まる → Resumeで連続更新へ戻ることを確認してください。

Play開始時に配置・編集状態・複数選択・保存先と新規シーン状態・Undo/Redo履歴を記録し、Stopでまとめて復元します。ResumeとStepは最初の記録を維持します。
通常は編集シーンが独立しているためモデルの再読み込みは不要です。配置が変わっていた場合だけ再構築し、復元失敗時は現在の編集データと実行用シーンを維持してPausedにします。
手動確認: 未保存の編集とRedo可能な履歴を作る → Play/Pause/Step/Resume → Stop → 配置・複数選択・保存先・未保存表示が戻り、Ctrl+Z/Ctrl+YがPlay前と同じ履歴をたどることを確認してください。

ProjectはModel/Scene/Texture/Audio/Shader/Fontを種類別に表示し、AssetsとShadersを参照します。画像・音声・フォントの登録は情報表示用で、シーンへの追加はModelだけです。
アセットをクリックするとInspectorに種類・Content相対パス・ファイルサイズを表示します。OBJ・Scene JSON・シェーダーは先頭8192バイトまで読み取り専用でプレビューします。Refreshで情報を更新します。
アセットの参照は配置と未保存状態を変更しません。HierarchyまたはSceneで対象を選ぶとInspectorはオブジェクト編集へ戻ります。
手動確認: ProjectのTypeで画像・シェーダーなどを絞り込む → アセットを選んでInspectorの種類・サイズ・ソースを確認 → Hierarchyを選んでTransform編集へ戻ることを確認してください。

アセットInspectorの表示中はSceneのギズモ・Delete・Ctrl+Dを無効にし、以前のScene選択へ誤適用しません。アセットファイルの削除・複製はまだ操作対象に含めません。

## 機能拡張前の画面確認

[手動チェックリスト](ManualChecks.md)と`Content/Assets/Scenes/EditorAcceptance.json`を使用してください。親子・複数選択・Play・Inspector・保存・ドッキングの確認を項目別に進められます。自動テストの成功と画面確認の完了は別に記録します。

## アセットの実表示プレビュー

Projectで画像を選ぶとInspectorに画像寸法・拡張子とRGBA8プレビューを表示します。WIC対応画像の先頭フレームを読み込みます。未対応／破損画像はConsoleとInspectorへ理由を表示します。
モデルは専用の512×512描画先で表示します。画像上の左ドラッグで周回、ホイールでズーム、Reset preview cameraで初期視点へ戻ります。Scene／Gameのカメラ・配置・履歴は変更しません。
Refreshで情報とプレビューを再読み込みします。GPU完了後に候補を読み込み、成功した場合だけ差し替えます。同じアセットの更新に失敗した場合は直前のプレビューを保持します。

## アセット変更の再読み込み

Assets内のリソース（OBJ・MTL・テクスチャなど）とShaders内のHLSL／インクルードの変更・追加・削除を0.5秒間隔で監視します。連続した変更が落ち着いてから一括更新します。Scene JSONの変更は監視対象外で、編集中のシーンを置き換えません。
Playing／Paused中と編集操作中は更新を保留し、Editingへ戻って操作が終わった後に反映します。ProjectのReload assetsから明示的にも更新できます。
プロジェクトのHLSL（VSMain／PSMain）を検証し、現在の未保存配置から新しいモデル・テクスチャ・パイプラインを準備します。すべて成功してから差し替えます。失敗した場合は表示・配置・選択・未保存表示・Undo/Redo履歴を維持し、Consoleへ理由を表示します。失敗後はファイルを修正するかReload assetsで再試行してください。
この段階では使用モデル全体を再構築するため、大きいシーンの更新時には待ち時間があります。

## 空オブジェクトとComponent保存形式

HierarchyのCreate emptyでTransformだけを持つオブジェクトを作成できます。親子付け・ギズモ・複数選択・複製・削除・Undo/Redo・保存に対応します。モデルを持たない対象はHierarchyで選択してください。選択枠とFフォーカスには原点付近の小さい境界を使います。
version 4ではTransformを必須データとして持ち、任意のMeshRenderer／Rotator／Camera／DirectionalLight／Sky／ParticleEmitter／CameraSway／Canvas／RectTransform／Image／Text／Button／AudioSourceをcomponents配列へ保存します。Componentにはオブジェクト内で一意なID、種類、enabledと型付きの設定を持たせます。TransformのIDはtransformとして予約します。未対応の種類・重複・不正なプロパティは読み込みを拒否し、黙って削除しません。
旧version 2のmodelは読み込み時にMeshRendererへ移し、ローカルTransformと親子を維持します。保存するとversion 4になり、モデルの参照先はMeshRendererだけが保持します。version 1は従来の変換スクリプトでローカル座標へ変換してから開いてください。

## Componentの編集と実行

単体選択のInspectorでAdd Componentから各Componentを追加できます。同じ種類は一つずつです。MeshRendererはProjectにあるAssets/Models配下のOBJを選び、有効切り替え・参照先変更・削除ができます。Reset MeshRendererは有効状態を戻し、モデル参照とIDを保持します。
RotatorはローカルX／Y／Zの角速度を度／秒で編集します。有効切り替え・リセット・削除にも対応します。Reset Rotatorは[0,90,0]とenabled=trueへ戻し、IDを保持します。有限値かつ各軸±100000度／秒以内に検証します。Componentの追加・削除・変更はUndo/Redoと未保存表示へ反映し、速度の一回のドラッグは一回のUndoになります。
Transformは必須で削除できません。Component変更は描画の外で処理し、モデルの読み込み失敗時は直前の設定と履歴を維持します。複数選択中のComponent一括編集はこの段階の対象に含めません。
RotatorはPlayの独立した実行用シーンでだけ更新します。Pauseで停止し、Stepで1/60秒、Resumeで継続します。Stopで実行用の回転を破棄し、編集中のTransform・設定・選択・履歴・保存先へ戻ります。空の親へ付けると子孫もTransform継承で回ります。
`Assets/Scenes/ComponentDemo.json`を開いてPlayすると、空のSpinnerの子が周回し、Peerは静止します。編集する場合は別名保存してから操作してください。


## シーンから作る背景・カメラ・照明

HierarchyのScene settingsを押すと、Inspectorで背景色・Game camera・霧を編集できます。背景色はSkyがない／無効なときに見えます。霧は有効切り替え・色・開始／終了距離・濃さを持ちます。

Create emptyでオブジェクトを作り、InspectorのAdd Componentから機能を追加します。位置・回転はTransformで編集します。各ComponentのEnabled・Reset・RemoveはUndo/Redoに対応し、ResetはComponent IDを維持します。色や数値の一回のドラッグは一回のUndoへまとめます。

- Camera：縦画角、Near/Far clip、基準縦横比、狭い画面で横構図を維持する設定。位置は親の拡縮・回転・移動を継承し、向きは親と自分のXYZ回転を継承します。Game cameraで明示指定するか、未指定なら最初の有効なCameraを使用します。明示指定したCameraが無効なら別のCameraへ自動切り替えしません。削除すると参照を解除し、Undoで復元します。
- DirectionalLight：ローカル方向、色、直接光・環境光・反射の強さ、反射の鋭さ。向きは親と自分の回転を継承します。現在はシーン内の最初の有効なDirectionalLightを使用します。照明がないシーンのモデルは暗くなります。
- Sky：空の上下色、地平線高さ、太陽の色・位置・大きさ・強さ、雲の上下色・不透明度・UV移動速度、3つの雲群の位置・大きさ。大きさ0でその雲群を隠せます。背景は画面に描くためTransformでは動かしません。最初の有効なSkyを使用します。
- ParticleEmitter：0〜1024粒、色・不透明度、ワールド単位の大きさ、ローカル発生範囲、周期ごとの移動量、周期、横揺れ。Transformと親子関係が発生範囲と軌道へ反映されます。複数の有効なEmitterを描画します。現在の粒は発光する丸形です。
- CameraSway：カメラ軸XYZ方向の揺れ幅（ワールド単位）と周期。同じオブジェクトのCameraへPlay中だけ加え、保存したTransformを書き換えません。

Sceneは編集用の自由カメラ、Gameはシーン内のCameraを使います。Play中は両方とも実行用の配置・照明・Sky・粒子を描画します。Pause／Step／Resume／Stopと、未保存状態・選択・保存先・Undo/Redoの復元は従来どおりです。アセット再読み込みは空・粒子の描画リソースも候補を先に生成し、失敗時は直前の表示と編集中のシーンを維持します。

空の新規シーンにはCamera・照明・Sky・粒子を自動追加しません。Gameでモデルを表示する場合は、Create empty → Add Component → Cameraと、別の空オブジェクト → DirectionalLightを作成してください。Cameraの配置例は位置[0,3,-10]・回転[6,0,0]度です。モデルを原点付近へ配置し、必要に応じて構図を調整します。Sky・ParticleEmitter・CameraSwayは必要に応じて追加します。旧version 2／3の外部シーンも同様に環境を追加できます。

TitleStreet.jsonとUiCheck.jsonにはCamera・DirectionalLight・Sky・ParticleEmitterがあり、CameraにCameraSwayが付いています。ComponentDemo.jsonとEditorAcceptance.jsonにもCamera・照明・Skyを保存しています。AppのタイトルはTitleStreet.jsonを使用し、背景専用の固定カメラ・専用シェーダーは使用しません。保存した変更は次のビルドでAppへコピーされます。タイトルUI・BGM・効果音も同じシーンへ保存し、共通ランタイムで描画・再生します。

手動確認はManualChecks.mdのK〜Pを最新ビルドのEditorで実施してください。自動テストの成功と画面確認の結果は別に記録します。


## UIと音声を作る

### キーフレームで演出を作る

Inspectorの「コンポーネントを追加 → アニメーション」で、対象オブジェクトの演出を作れます。
3D位置・3D回転・UI位置・UIサイズ・UI回転・不透明度を、昇順の時刻と値で保存します。
回転はラジアン、位置・サイズは既存Transform／RectTransformと同じ座標系です。
値はX/Y/Z/Wの4数値で保存し、2D位置・サイズはX/Y、回転・不透明度はXを使用します。
等速・滑らか・減速・行き過ぎて止まる補間、遅延、ループをトラックごとに選べます。

時計は「登場・経過時間」（sceneTime）、「待機」（motionTime）、「開始」（startTime）を選びます。
開始時計はCanvas状態のstartRequested=1で起動します。負のstartTimeは未開始です。
同じプロパティに複数トラックがあると、後の開始済みトラックが優先されます。
CanvasのState defaultsで時計を設定すると編集プレビューのUIを確認できます。
Play → 一時停止でツールバーの「演出時刻」「開始時刻」をドラッグすると、カメラとUIを同じ時刻へ移せます。
Stopで実行用の配置と時計を破棄し、編集データへ戻ります。
Animation ComponentはJSONへの保存／読み込み、複製、Undo／Redoに対応します。

最初はProjectから`Assets/Scenes/UiAudioDemo.json`を開いて別名保存してください。Cameraを持たない2DシーンでもUIを描画できます。PlayでBGM、パネル切り替え、効果音、Play終了のボタンを確認できます。

1. Create empty → Add Component → Canvas。Reference resolutionは基準の画面サイズです。実画面へ縦横比を保って収めます。Scale with screenをOFFにすると画面全体をピクセル座標で使い、全面の覆いなどを作れます。
2. Canvasの子を作り、RectTransformとImageまたはTextを追加します。UI位置は左上が原点で下方向が正です。3D TransformはUI配置に使いません。
3. RectTransformのAnchor min/max・Pivot・Position・Size・Rotationを編集します。アンカーの範囲を広げると親のサイズに合わせて伸びます。回転・表示状態は親UIから継承します。入れ子のCanvasは外側Canvasの座標系を使います。
4. Game上でクリックしてUIを選び、選択済みUIをドラッグして移動します。右下の黄色いハンドルでサイズ変更できます。Hierarchy選択も使えます。ドラッグ一回をUndo一回で戻せます。
5. ImageはProjectの画像をAssetで割り当て、色・透明度・UV範囲を編集します。空のAssetは単色の四角形です。TextはUTF-8文字列・Windowsのフォントファミリー・文字サイズ・色を編集します。文字はRectの固定サイズへ折り返し、範囲外を切ります。伸縮アンカーでは文字の描画も伸縮します。描画用文字画像は各辺4096pxまでです。
6. ボタン用オブジェクトへButtonを追加し、Hover/Pressed tintとActionを設定します。親Canvas、RectTransform、有効なButtonが必要です。子TextにButtonを付けなくても親ボタンがクリックできます。描画順は保存したobjectsの順で、後のUIほど前面になります。
7. 空オブジェクトへAudioSourceを追加し、Asset・Volume・Loop・Play on awakeを設定します。InspectorのAudition／Stop auditionで試聴できます。Projectの音源Inspectorからも試聴可能です。OSのデコーダー対応形式を使用し、読み込み失敗をConsoleへ表示します。現在は2D音声です。

### ボタンの動作とUI状態

| Action | Target |
| --- | --- |
| show / hide / toggle | 表示を切り替えるオブジェクトID。子UIにも反映 |
| playAudio | AudioSourceを持つオブジェクトID |
| loadScene | Assets/Scenes配下のJSONパス |
| quit | EditorではPlay終了、Appでは終了要求 |
| setState | `screen=1&volume=5`のような状態値の代入 |
| click | ゲーム処理へUiEventを通知 |

Click audio objectにはクリック音のAudioSource IDを指定します。Game eventはAppの処理に渡す識別子です。複製した対象に含まれるボタンのTarget／音源参照は複製先IDへ移します。setStateの値やシーンパスはそのままです。

CanvasのState defaultsで初期状態値を保存できます。RectTransformのVisible whenは`screen=1&selected=0`のような一致条件です。空なら常に表示します。X offset／Opacity／Width bindingは状態キーを指定します。Widthは0〜1の倍率です。Intro delay／X offsetは状態キー`intro`の0〜1進行率に対応します。編集プレビューでもState defaultsを使うので、分岐画面のレイアウトを確認できます。

Playは独立したUI状態と音源を使用します。Pauseとフォーカス喪失で音声の再生位置を保持して停止し、Resume／復帰で再開します。Stepは一フレームの動作を進め、音声は停止したままです。Stopで音源・UI状態を破棄し、編集データ・選択・Undo/Redoを復元します。Play開始時に試聴を停止します。

TitleStreetのロゴ・メニュー・設定表示・遷移帯・5音源はHierarchyから編集できます。タイトル固有の選択、設定保存、音量フェード、Game開始はAppのTitleMenuから状態／イベントで接続します。EditorのStartボタンのloadScene先はComponentDemoで、変更可能です。Appでは`menu:0`／`start`イベントが既存Gameへの開始を扱います。Game eventを空にすれば汎用Actionを使います。

C++のJSON処理はnlohmann/json 3.12.0へ統一しています。依存ヘッダーとMITライセンスを同梱し、ビルド時のダウンロードは不要です。シーンversion 2〜4を読めます。ユーザー設定はsettings.jsonへ保存し、旧settings.txtも読み込みます。保存・再読み込み・UndoのJSON生成は同じ実装を使います。


## エディターのImGuiフォントと日本語

英数字はFira Mono Regular、日本語はM PLUS 1p Regularを同じImGuiフォントへ合成し、16pxを基準に表示します。Fira MonoのASCIIを維持し、同梱の日本語フォントでひらがな・カタカナ・漢字・日本語記号を補います。ImGui 1.92の動的字形読み込みを使い、旧GetGlyphRangesJapaneseの常用漢字リストで制限しません。

フォントはContent/Assets/Fontsから読み込み、配布ビルドにもTTFとOFLライセンスをコピーします。Windowsへのフォントインストールは不要です。欠損時は起動エラーを表示し、別のフォントへの無言の置き換えは行いません。フォントを更新した場合はEditorを起動し直してください。出典とSHA-256はContent/Assets/Fonts/README.mdに記録しています。

エディターのメニュー、パネル、ツールバー、コンポーネントの設定項目、保存確認、操作結果は日本語で表示します。ドッキングの標準メニュー・説明も日本語です。InputTextは既存のWin32 Unicode／IME入力を使ってUTF-8の値を編集します。英語表記の旧パネル配置は読み込み時に移行し、位置・サイズ・ドッキング・選択タブを引き継ぎます。

シーンJSONの項目名・コンポーネント種別・ボタン動作キー、アセットのファイル名、ユーザーが入力した名前や状態キーは変更しません。ボタンの動作は日本語で選び、従来の英語キーを保存します。エンジン・OS・シェーダーコンパイラーが返す技術的な診断文は原文を維持します。Game内のText Componentはシーンに設定された内容・フォントを使います。本文中のHierarchy、Inspector、Project、Playなどは、それぞれヒエラルキー、インスペクター、プロジェクト、再生に対応します。

手動のIME候補表示・確定操作はManualChecks.mdのQ、日本語UIの確認はRを使ってください。

## プレイヤー操作

Inspectorの「コンポーネントを追加 → プレイヤー操作」でPlayerControllerを追加し、移動速度を設定できます。Play中にGame画像へカーソルを置き、WASDまたは矢印キーで親のローカルXZ平面を移動します。斜め移動の速度は一定で、長いフレームの移動時間は0.1秒までです。Appの編集済みシーンでも同じキーで操作できます。箱の衝突判定を持つプレイヤーは、有効な箱の衝突判定を持つ床や壁で止まります。重力を使用すると世界の下向きへ落下し、接地中にSpaceでジャンプできます。設定の保存、複製、Undo／Redoに対応し、Stopでは編集時の配置に戻ります。

## 衝突・重力・ジャンプ

Projectから`Assets/Scenes/PhysicsPlayground.json`を開き、PlayしてGame上へカーソルを置くとWASD／矢印キーでPlayerを移動、Spaceでジャンプできます。床・壁・台を配置した確認用シーンです。
Inspectorの「箱の衝突判定」はローカルの中心・大きさを編集でき、親の移動・回転・拡縮を継承します。回転時はワールド軸の外接箱で判定するため、斜めの面や斜面を正確に歩く用途には対応しません。配置時に重なっている箱の押し出し、物体同士の反動、移動床への追従はありません。プレイヤーとその親子に属する箱は自己衝突から除外します。
PlayerControllerの「重力を使用」「重力加速度」「ジャンプ速度」を調整できます。旧シーンの重力はOFFを維持します。再生中はGameからカーソルを外しても重力は進み、Pauseは停止、Stepは入力なしで1/60秒だけ進みます。Stopと再生し直しで落下速度もリセットされます。
