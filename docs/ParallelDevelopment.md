# エディター修正とゲーム制作の並行開発

ゲーム制作は元のWP1チェックアウトの `Games/GuruttoWorld`、エディター修正は別worktreeで進めます。
同じリポジトリでも作業フォルダー・ブランチ・ビルド出力が別になるため、エディターの修正途中のコードや実行ファイルが制作中のゲームへ直接混ざりません。

| チャット | 作業場所・担当 |
|---|---|
| ぐるっとワールド ゲーム制作 | 元のチェックアウト。`Games/GuruttoWorld/Assets`、`Shaders`、ゲーム設定・制作データ |
| WP1 エディター修正 | 別worktree。`Editor`、`Engine`、`SceneRuntime`、共通プレイヤー・ビルド処理、関連テスト |

ゲーム専用ソリューションは `Games/GuruttoWorld/GuruttoWorld.slnx`、エディター専用ソリューションは `Editor.slnx` です。
ルートの `Game.slnx` も移行済みゲームをビルドします。`WP1.slnx` は全体開発用です。

ゲーム制作でエディターの不具合を見つけたら、対象シーン・素材のパス、操作手順、期待する結果をエディター修正用チャットへ伝えます。
エディター側では、worktreeにあるゲームの基準データや `generated` 内に複製した検証用プロジェクトで再現します。制作中のゲームの素材を直接変更しません。

エディター修正は関連テストとビルドを確認してコミット単位でまとめます。
検証済みの修正を元のチェックアウトへ取り込むタイミングは、ユーザーが決めます。
ゲーム側は取り込み後に専用ソリューションを再ビルドし、制作中のシーンで確認します。

両チャットの初回作業は担当範囲と現状の確認です。機能追加やゲーム内容の変更は、それぞれのチャットへ具体的な依頼を送って進めます。

## エディターとプレイヤーの再生処理

エディターのGame再生と配布プレイヤーは、`SceneRuntime::GamePlayback` と `TitleController` を使用します。
タイトルの選択・決定、CONFIG演出、設定の読み書き、音量・操作音、ゲーム入力・移動、UIのクリック・ホバー・ショートカット、毎フレームの環境更新をここで扱います。
新しいゲームの動作は共通処理またはゲームプロジェクトのScriptへ追加し、AppとEditorに同じ処理を別々に書かないでください。
設定は両方とも `GameSettings::UserPath()` を使います。同じ再生セッションでは、タイトルの登場演出を初回だけ再生します。
Editorは編集データの複製・再生/一時停止/コマ送り、Gameパネルの座標変換を担当し、Appはウィンドウ入力の取得を担当します。
QUITの共通コマンドは、Editorでは再生停止、Appではウィンドウ終了として受け取ります。
設定画面は `Assets/Scenes/Settings.json` へ遷移し、保存・取消後はカメラと色を滑らかに戻してタイトルへ遷移します。接近先・戻り先・時間はCanvasのメニュー設定とシーンのTransformで編集します。白黒化は不透明度を変更せず、Materialの `effectAmount` を使用します。
START／QUITの決定後は、MenuEntryの `transitionCameraTarget` に指定したTransformへ、決定時のカメラ位置から移動します。時間はCanvasの `startDuration` で設定し、カメラが到着した最後のフレームを描画してからロード・終了を通知します。`titleEffectTime` はCONFIGまたは決定後の共通演出時計、`departureKind` は未決定=-1・START=0・QUIT=1です。Materialの `effectVariant`（0=黒・1=白・2=白黒）をAnimationで切り替え、STARTでは道沿いの建物3棟・奥の高層ビル5棟と道路・歩道を白く、それ以外は黒くして道路を進み、QUITではEXITの白い文字と矢印を残して左へ進みます。一時停止・コマ送りも同じ再生処理を使用します。
`GamePlaybackParityValidation` は同一の診断入力を実際のAppとEditor再生経路へ渡し、各フレームのUI値・Transform・Material・STARTコマンドとシーン切り替えを比較します。
CONFIGの一時停止・コマ送り・復元・再再生は `TitleConfigTransitionValidation` で検証します。
