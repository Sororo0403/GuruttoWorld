# 汎用UI

`Content/Assets/Scenes/UiControlsDemo.json` をEditorで開き、PlayしてGameビューを操作すると、入力欄・Slider・Toggle・スクロールと自動レイアウトを試せます。配布Appも同じSceneRuntime経路を使います。

Inspectorの「コンポーネントを追加」から各UIを追加できます。RectTransformも自動追加されます。Canvasの子孫に配置してください。編集・保存・複製・Prefab・Undo/Redoは通常のComponentと同じ経路です。

| Component | 操作・設定 |
|---|---|
| InputField | クリックでフォーカス、文字入力、BackspaceでUnicode文字を削除、Enterで確定、Escapeか外側クリックでフォーカス解除。最大文字数・複数行・パスワード・読み取り専用を設定可能。Textを追加するとフォント・色を設定できる。 |
| Slider | クリックとドラッグで値を変更。最小・最大・整数・縦向きと塗り色を設定できる。 |
| Toggle | クリック解放で値を切り替え、チェック時の色を表示する。 |
| ScrollView | 子孫をホイールと背景ドラッグで移動する。コンテンツサイズ・初期位置・方向を設定し、表示範囲外の描画とヒットを制限する。 |
| Mask | 子孫の描画・ヒットを矩形の表示範囲内へ制限する。 |
| LayoutGroup | 直下のRectTransformをHierarchy順でhorizontal・vertical・gridに配置する。間隔・余白・セル・列数・幅／高さの展開を設定する。 |

Slider/Toggleの`binding`は`SceneEnvironment::Ui().values`、InputFieldは`Ui().strings`のキーを指定します。空欄はオブジェクトIDを使います。ScrollViewの実行時位置は`Ui().scrollOffsets`に保存します。これらはPlay状態であり、操作によってシーンの初期値を書き換えません。

`changedEvent`と`submittedEvent`を指定するとScriptへ通知します。`ScriptContext::event`の`name`に指定名、`sender`にUIオブジェクトID、`value`にSlider/Toggleの値、`text`に入力欄のUTF-8文字列が届きます。入力欄のフォーカス中は移動とショートカットを抑制します。

Scriptから表示値を変える場合は`ScriptScene::SetUiValue("volume",0.5f)`と`SetUiText("playerName","Player")`を使います。FixedUpdate・Update・LateUpdateの予約を順に合成し、フレーム全体が成功した場合だけ実行中のbindingと文字描画へ反映します。失敗したフレームの予約は破棄されます。Componentの`value`や`text`は初期値であり、すでに存在するbindingを変更する用途にはこのAPIを使ってください。

UI予約は1フレーム合計256キーまで、キーは空でない128バイト以下、数値は有限な±100000以内、文字列は有効なUTF-8で4096バイト以下です。ネイティブScriptModule ABIはversion 3で、ScriptSceneのサイズも照合するため、古いDLLは再コンパイルしてください。

表示範囲のクリップはCanvas座標で評価するため、GameのオーバーレイとSceneの3D Canvasで共通です。回転したマスクのクリップ範囲は矩形の外接矩形です。
