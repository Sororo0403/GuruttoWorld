# エディターの手動確認

このチェックリストは画面上の入力・表示を確認するためのものです。自動テストの成功だけでは完了扱いにしません。結果は各項目を「OK／NG／未確認」で記録してください。

## 準備

1. DebugまたはDevelopmentのEditorをビルドして起動する。
2. File → Open sceneで`Assets/Scenes/EditorAcceptance.json`を開く。
3. File → Save asで`EditorAcceptanceLocal.json`など別名の作業用シーンに保存する。確認用の原本へは上書きしない。
4. View → Reset panel layoutで配置を戻す。Hierarchyの親子を展開し、ParentとPeerをCtrlで選択してScene上でFを押す。

確認用シーンには8個のモデルとCamera・Directional Light・Skyがあります。Parent → Child → Grandchildは通常の三段階層、Peerは独立したモデル、MirrorはX軸が反転したモデルです。ShearParent → ShearChildは非一様拡縮と子の回転によってせん断を含む階層です。Groundは床です。

各項目は作業用シーンの保存直後から始めます。次の項目へ移る前にReloadし、未保存確認では「破棄して読み込み」を選びます。Gの保存確認後は原本を開いて新しい作業用シーンを作り直してください。文字入力中はEnterで確定してからSceneをクリックし、ショートカットを使用します。回転のInspector表示は度、JSONの保存値はラジアンです。

## A：親子と移動

1. Parentを選び、Local positionのXを-5から-3へ変更する。
2. ChildとGrandchildもX方向へ2移動し、選択枠とクリック位置が描画に合うことを確認する。Childのローカル位置は[3, 0, 0]のまま。
3. Undoして戻し、ParentとChildをCtrlで選んでMoveギズモを一回ドラッグする。
4. 子孫が一緒に動き、Childが二重に移動しないことを確認する。Undo一回で戻り、Redo一回で再現する。
5. 戻した後、ChildをPeerへドラッグして親を変更する。画面上の配置は変わらず、Undoで元の親子に戻る。

## B：複数選択の回転・拡縮・フォーカス

1. Parent、Peerの順にCtrlで選ぶ。Peerが主選択になる。
2. Scene上でFを押し、両方のモデルと選択枠が収まることを確認する。
3. Rotate（2）で回す。Peerを中心にParentの位置と向きが変わり、子孫が追従する。Local／WorldとSnapも試す。
4. Undo一回でドラッグ前へ戻す。Scale（3）の中央の一様拡縮ハンドルをドラッグする。モデルの大きさとPeerからの間隔が一緒に変わる。
5. Undo／Redoで一括復元できる。ParentとChildの同時選択でも回転・拡縮が二重に適用されない。

## C：複製・削除

1. ParentとChildだけを選びCtrl+D。コピー同士が親子になり、元の配置からワールドX方向へ4ずれる。Grandchildは複製されない。
2. コピー全体が選択される。Undo一回で消え、Redoでコピーと複数選択が戻る。
3. ReloadしてParentとChildだけを選びDelete。Grandchildは同じワールド配置でルートに残る。
4. Undo一回で元の親子と複数選択が戻る。Redoで再び削除される。

## D：反転と操作の拒否

1. Mirrorを選びF。反転したモデルが表示され、選択枠・クリック選択が合うことを確認する。
2. ShearChildの親を解除する。せん断をローカルSRTへ保存できないため操作は拒否され、配置・親子・未保存状態が変わらず、理由が表示される。
3. ShearParentだけを削除しても同じ理由で全体が拒否される。
4. ShearParentとShearChildを両方選んで削除すると成功する。Undo一回で両方戻る。

## E：Play／Pause／Step／Stopと履歴

1. Parentの名前と位置を編集する。位置を別の値へもう一回変更してUndoし、Redo可能な履歴と未保存表示を作る。ParentとPeerを複数選択する。
2. Play。Gameに未保存の配置と空・光の粒が表示され、時計が進む。配置編集・保存・Undoは無効になる。
3. Pauseで時計と背景の動きが止まる。Stepを二回押すと更新数が二回増え、時間が合計1/30秒進む。押していない間は止まる。
4. Resumeで連続更新へ戻り、Stopで編集へ戻る。
5. 名前・位置・複数選択・保存先・未保存表示がPlay前と同じ。RedoがPlay前の二回目の位置変更を再現し、Undoも履歴をたどれる。

## F：ProjectとInspectorの切り替え

1. ProjectのTypeでShaderを絞り込み、アセットを選ぶ。Inspectorに種類・Content相対パス・サイズ・ソースが表示される。
2. Scene上でDelete／Ctrl+Dを押しても以前の選択モデルへ適用されない。未保存状態も変わらない。
3. HierarchyでParentを選ぶとTransform編集へ戻る。Textureなど他の種類も選んで情報を確認する。
4. Refresh後も一覧を使用できる。画像を選ぶと寸法と画像が表示される。モデルを選びプレビュー上で左ドラッグ・ホイール・Reset preview cameraを試し、Scene／Gameの視点や未保存状態が変わらないことを確認する。

## G：保存・読み込み・新規シーン

1. Parentの名前と位置を編集して保存する。未保存表示が消える。
2. Reloadと別シーンからのOpenで、保存した配置と親子が戻る。
3. New sceneでモデルを追加し、CameraとDirectionalLightも空オブジェクトへ追加する。未保存のままPlay → Stop。新規シーンの状態を維持し、初回保存で保存先を指定できる。
4. 未保存でReloadと終了確認を開きCancel。現在の配置と選択が維持される。

## H：ドッキングと画面サイズ

1. SceneとGameを分割して並べ、サイズを変更する。表示の縦横比が正しく、Sceneのクリック位置・ギズモが画像と一致する。
2. Project／Inspectorも移動し、エディターを終了して再起動する。パネル配置が復元される。
3. View → Reset panel layoutで標準配置へ戻せる。

## 結果の報告

例：`A OK、B OK、C NG（3で孫が消えた）、D 未確認、E OK、F OK、G OK、H OK`。
NGには「操作した対象・手順番号・期待した結果・実際の結果」を添えてください。すべて未確認から開始し、画面を操作した項目だけOKにします。

## I：Componentと実行

1. ComponentDemo.jsonを開き、別名保存する。Spinnerを選び、Rotatorの速度とIDを確認する。
2. 速度を一回ドラッグして変更し、Undo一回で戻ることを確認する。Enabledの切り替え・Reset Rotator・Remove Rotator・Add Component → RotatorもそれぞれUndo/Redoする。
3. Create emptyで新しい親を作り、Add Component → MeshRendererでモデルを選ぶ。参照先変更・無効化・削除後もTransformと親子が残る。
4. SpinnerをPlayし、子が周回してPeerが静止する。Pauseで止まり、Stepで一回ずつ進み、Resumeで続く。Stopで編集時の配置と設定・選択・履歴へ戻る。
5. 親子を複製し、Componentの設定とコピー同士の親子を確認する。保存して開き直しても設定が戻る。

## J：アセット更新

1. 編集用のコピーのモデル・画像・シェーダーで確認する。ファイルを外部で変更し、編集が落ち着いた後に表示とプレビューが更新されることを確認する。
2. 未保存の配置とRedo可能な履歴を作ってReload assets。配置・選択・未保存表示・履歴が維持される。
3. 画像の破損やシェーダーの構文エラーで更新を失敗させる。Consoleに理由が出て以前の表示が保持される。ファイルを戻すと再更新できる。
4. Play／Pause中にファイルを変更すると更新が保留され、Stop後に反映される。
5. Scene JSONを外部で変更しても未保存の編集中シーンは置き換わらない。シーンを更新する場合は明示的なReloadを使う。

I・JもOK／NG／未確認で報告してください。


## K：背景・Camera・照明・Sky

1. 最新ビルドのEditorを起動し直し、TitleStreet.jsonを開いて別名保存する。HierarchyにCamera／Directional Light／Sky／Light Particlesがあることを確認する。
2. Scene settingsでGame cameraを確認する。CameraのTransform・縦画角・Near/Far clipを変更し、Gameの構図が変わることを確認する。親の空オブジェクトへ付け、親の位置・XYZ回転を変更して追従を確認する。Undoで戻す。
3. Cameraを複製し、Scene settingsでコピーへ切り替える。位置を変更して切り替えを確認する。指定中のCameraを無効にするとGameのモデル表示が止まる。削除／Undoで指定とComponentが復元する。
4. Directional Lightの色・方向・強さ・環境光・反射を変更する。SceneとGameに反映し、親の回転で照明方向が変わることを確認する。
5. Skyの上下色・太陽・雲の位置・大きさ・不透明度を変更する。両ビューへ即時反映する。色を一回ドラッグしてUndo一回で元へ戻す。雲群の大きさ0でその群が消える。
6. Skyを無効にし、Scene settingsの背景色を変更する。背景色が見える。Skyを戻し、霧の色・距離・濃さ・有効切り替えを確認する。
7. カメラ・照明・SkyのEnabled／Reset／Remove／AddをそれぞれUndo/Redoする。保存して開き直し、設定とIDが戻る。

## L：粒・揺れ・Play

1. Light Particlesを選び、粒数・色・不透明度・大きさ・発生範囲・移動量・周期を編集する。粒数0で消え、Undoで戻る。
2. 発生オブジェクトの位置と親を変更し、粒の発生範囲と軌道が追従することを確認する。Emitterを複製して両方が描画されることを確認する。
3. CameraのCameraSwayの揺れ幅・周期、SkyのCloud velocityを変更し、Playで動くことを確認する。SceneとGameは同じ実行状態を描画する。
4. Pauseで雲・粒・視点・Rotatorが止まり、Step一回で一フレームだけ進み、Resumeで続く。描画だけでは時間が進まない。
5. Stopで編集時の設定・Transform・選択・保存先・未保存表示・Undo/Redo履歴が戻る。揺れがTransformへ書き込まれていない。
6. ParticleEmitter／CameraSwayのEnabled／Reset／Remove／Addと、保存・開き直しを確認する。CameraがないオブジェクトのSwayは視点を動かさない。

## M：新規作成・App・再読み込み

1. New sceneを作る。背景だけでCamera・Sky・粒が自動生成されないことを確認する。
2. 空オブジェクトへCameraを追加し、位置[0,3,-10]・回転[6,0,0]度へ変更する。別の空オブジェクトへDirectionalLightを追加する。原点付近にモデル、別の空オブジェクトにSkyも追加してGameに表示する。必要ならParticleEmitterとCameraSwayを追加する。
3. 別名保存 → 別シーンを開く → 作ったシーンを開き直す。環境と配置・親子が再現する。
4. Content/Shaders/Sky.hlsl・GlowParticle.hlsl・Mesh.hlslを退避してから編集し、Reload assetsを確認する。確認後は退避した内容へ戻す。空・モデル・粒が更新し、未保存のシーン・選択・履歴が維持される。構文エラー時は以前の表示を維持し、復旧後に更新する。Play中の更新はStop後に反映する。
5. TitleStreet.jsonをAppで使う設定へ保存する場合は原本を退避して行う。次のビルドでAppの背景へ反映し、背景演出OFFで揺れ・雲の移動が止まり粒が消える。UIと音声は従来どおり。確認後に退避した原本を戻す。

K〜Mも手動で操作した項目だけOKとし、未操作は未確認のままにしてください。


## N：UIの作成・配置・保存

1. 最新Editorを起動し直し、UiAudioDemo.jsonを別名保存する。Gameに日本語見出し・パネル・3ボタンが表示される。
2. HierarchyのTextを選び、文字・フォント名・文字サイズ・色を変更する。Imageの画像・色・透明度・UVを変更する。保存・開き直しで再現する。
3. GameでUIを選択し、ドラッグで移動、黄色い右下ハンドルでサイズ変更する。Undo一回で一回のドラッグが戻り、Redoで復元する。回転した親の子でも確認する。
4. Canvasの基準サイズとRectのアンカー・ピボットを変更する。Gameの幅・高さを変えて位置と伸縮を確認する。親UIを非表示にすると子も消える。
5. UI親子と音源をまとめて複製し、コピーのボタンがコピー側を操作する。Componentの追加・削除・Reset・Undo/Redoも確認する。
6. 新規シーンでCanvas → 子のRectTransform → Image／Text／Buttonを作り、保存して開き直す。CameraなしでもUIが表示される。

## O：ボタン・音声・Play

1. UiAudioDemoをPlayする。BGMが鳴り、ホバー／押下でボタン色が変わる。押したボタンの外で離すと動作しない。
2. パネル切り替えで表示が切り替わり、効果音ボタンで音が鳴る。Play終了ボタンでEditingへ戻り、BGMが止まる。
3. AudioSourceのAsset・Volume・Loop・Play on awakeを編集し、Audition／Stop auditionを確認する。Projectの音源Inspectorでも試聴する。Play開始で試聴が止まる。
4. Pauseで音が止まり、Step中も停止したまま、Resumeで続きから再開する。別アプリへの切り替えと復帰でも確認する。Stopで音が完全に止まる。
5. Buttonにshow／hide／toggle／playAudio／setState／loadSceneを設定し、Playで確認する。loadScene失敗時に直前の実行シーンが残る。Stop後に元の編集シーン・保存先・選択・履歴へ戻る。
6. 使用中の画像を退避して破損させ、Reload assetsの失敗時に直前のUIが残ることを確認する。戻して再読み込みする。未保存のUI配置が維持される。

## P：既存タイトルとJSON互換

1. TitleStreetを別名保存し、タイトルUIと5つのAudioSourceがHierarchyにあることを確認する。CanvasのState defaultsのscreenを1、screenNotSettingsを0にすると設定画面を編集できる。確認後はUndoで戻す。
2. UIの位置・切り出し・色・BGM音量を変更し、保存・開き直し、Playで確認する。原本をAppへ使う場合は原本を退避してから保存・ビルドする。
3. Appのタイトルで開始入力・メニュー・設定・マウスボタンとキーボード／ゲームパッドの既存操作、音量・背景演出設定・遷移を確認する。設定画面の保存／戻るも確認する。確認後は原本を復元する。
4. 既存version 2／3／4のシーンを開き、保存して再度開く。親子・ローカルTransform・Componentが保たれる。新規保存したJSONはnlohmann/jsonで処理する。
5. 旧settings.txtだけがある場合に設定が読み込まれ、保存後にsettings.jsonが作られることを確認する。確認に使った設定ファイルは退避して戻す。

N〜Pも、手動で操作した項目だけOKとし、未操作は未確認のままにしてください。


## Q：Fira Monoと日本語入力

1. 最新Editorを起動し直す。英数字がFira Monoになり、日本語の名前や文字列が四角い代替文字にならず表示される。
2. 作業用シーンのオブジェクトを選び、Inspectorの名前へIMEで「日本語の名前・複製・ｶﾀｶﾅ」を入力する。変換候補が入力欄の近くに表示され、確定後にHierarchyとInspectorの名前が読める。
3. Text Componentへ「ひらがな／カタカナ／漢字」を入力する。入力欄とGameに表示され、カーソル移動・Backspace・コピー＆ペーストを確認する。
4. 保存して開き直し、名前と文字列が維持される。Undo／Redoでも文字列が戻る。Projectの日本語ファイル名とConsoleの日本語ログも読める。
5. 英数字の細いiと広いWが同じ幅で表示され、日本語と混在しても行が欠けないことを確認する。ウィンドウサイズ変更・ドッキング後も読める。

Qは手動で操作した項目だけOKとし、IMEの画面操作は自動テストの成功とは別に記録してください。

## R. 日本語UI・既存レイアウトの引き継ぎ

- Editorを再起動し、「ファイル・編集・表示」、各パネル、再生・一時停止・停止・コマ送り、ギズモの移動・回転・拡縮が日本語で読めることを確認する。
- 既存のパネル配置が維持され、シーン・ゲームのタブ切り替え、「表示 → コンソール」、ドッキング・配置リセットが動くことを確認する。狭いパネルは幅を広げて設定項目を確認する。
- インスペクターでコンポーネントを追加し、位置・回転・拡縮、画像・テキスト・ボタン・音源の設定が日本語で表示されることを確認する。
- ボタンの「動作」で「表示切り替え」「音声再生」などを選んで保存し、JSONのactionがtoggle・playAudioなど従来の値であること、再生時も同じ動作をすることを確認する。
- シーンを変更してから別シーンを開き、未保存の確認ダイアログが日本語で表示されること、キャンセルすると編集が維持されることを確認する。名前を付けて保存・上書き・終了時の確認も確認する。
- 日本語の名前・テキストを入力し、保存・再読み込みで保持されることを確認する（IMEの確認はQ参照）。


## S. タイトルのキーフレーム演出

1. TitleStreet.jsonを開き、ゲーム表示とHierarchyで人物・群衆がなく、朝のCC0街とロゴを確認する。
2. scene-cameraまたはworld-logoを選択し、アニメーションを開く。時計・補間・時刻・値を変更してUndo／Redo、保存・再読み込みで保持を確認する。
3. 再生で登場を確認し、一時停止中の演出時刻を0〜3秒へ動かす。コマ送りと再開で同じカメラとUIが続く。
4. 一時停止中の開始時刻を-1→0→0.8秒へ動かす。帯が全画面を覆い、-1へ戻すと再びタイトルが見える。
5. 再生中に開始案内をクリックし、開始の演出を確認する。Appでは任意のキー・パッドボタンでも同じ演出後にGameへ進む。
6. AppでGameから戻り、登場を繰り返さず開始案内が表示される。ウィンドウ比率を変えてロゴ・案内と覆いを確認する。

Sの実機操作は未確認。エディター操作がユーザーのEscapeで停止したため、非表示GPU描画・ImGuiパネル描画・時計と入力の自動検証とは区別する。
