# エディターの手動確認

このチェックリストは画面上の入力・表示を確認するためのものです。自動テストの成功だけでは完了扱いにしません。結果は各項目を「OK／NG／未確認」で記録してください。

## 準備

1. DebugまたはDevelopmentのEditorをビルドして起動する。
2. File → Open sceneで`Assets/Scenes/EditorAcceptance.json`を開く。
3. File → Save asで`EditorAcceptanceLocal.json`など別名の作業用シーンに保存する。確認用の原本へは上書きしない。
4. View → Reset panel layoutで配置を戻す。Hierarchyの親子を展開し、ParentとPeerをCtrlで選択してScene上でFを押す。

確認用シーンには8個のモデルがあります。Parent → Child → Grandchildは通常の三段階層、Peerは独立したモデル、MirrorはX軸が反転したモデルです。ShearParent → ShearChildは非一様拡縮と子の回転によってせん断を含む階層です。Groundは床です。

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
3. New sceneでモデルを追加し、未保存のままPlay → Stop。新規シーンの状態を維持し、初回保存で保存先を指定できる。
4. 未保存でReloadと終了確認を開きCancel。現在の配置と選択が維持される。

## H：ドッキングと画面サイズ

1. SceneとGameを分割して並べ、サイズを変更する。表示の縦横比が正しく、Sceneのクリック位置・ギズモが画像と一致する。
2. Project／Inspectorも移動し、エディターを終了して再起動する。パネル配置が復元される。
3. View → Reset panel layoutで標準配置へ戻せる。

## 結果の報告

例：`A OK、B OK、C NG（3で孫が消えた）、D 未確認、E OK、F OK、G OK、H OK`。
NGには「操作した対象・手順番号・期待した結果・実際の結果」を添えてください。すべて未確認から開始し、画面を操作した項目だけOKにします。
