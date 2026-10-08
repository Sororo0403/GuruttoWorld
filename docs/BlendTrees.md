# Blend Tree

Animatorの状態のMotionに、クリップ・初期姿勢・Blend Treeを指定できる。
既存の`clip`だけを持つ状態は従来どおり再生する。
InspectorでTreeの方式、入力パラメーター、子、速度、入れ子の参照を編集する。
編集結果はシーン保存、Undo/Redo、Prefabのプロパティ上書きに含まれる。

| 方式 | 評価 |
|---|---|
| 1D | 閾値順に隣接する2つの子を線形に混ぜる。範囲外は端の子を使用する。保存順は問わない。 |
| 2D | 座標間の線形な重みの帯から各子の最小値を求め、全体を正規化する。子の座標ではその子だけを使用する。 |
| Direct | 各子のパラメーターの正の値を正規化して使用する。負の値は0、全体が0なら初期姿勢を使用する。 |

子には別のTreeを指定できる。重みと速度は親から子へ乗算する。
参照は同じAnimator内で解決し、循環参照・重複名・重複閾値・重複2D座標・未知のクリップを拒否する。
1つのAnimatorは最大32 Tree、各Treeは32子、深さ8、展開後256 Motionまで。
子の速度は0.001～1000、状態の速度は0～1000で、状態の速度0は再生時計を止める。

すべての子は同じ正規化位相で再生する。共通のサイクル長は`合計(重み × クリップ長 ÷ 子の速度)`。
入力値が変わっても位相は引き継ぎ、サイクル長だけを変更する。
初期姿勢の長さは1秒として扱う。状態のLoopが無効なら位相1の姿勢で停止する。
位置・拡縮は重み付き和、回転は最短弧のSlerpを使用する。3以上は展開した保存順に、合計重みでSlerpを繰り返す。
子の重みが0になる境界でも、2 Motionへの切り替えで回転が飛ばない。
遷移の終了時刻は正規化位相を使用し、通常のクリップとTreeの間でもクロスフェードできる。
不正な評価では時計・状態・姿勢を保持する。

`moveX`は`MoveRight - MoveLeft`、`moveY`は`MoveForward - MoveBack`、`speed`はその2軸の長さ。
`grounded`、`pressed:Action名`、入力Actionの値も参照できる。未指定のパラメーターは0。
Inspectorのパラメーター一覧で保存値を指定できる。保存値、入力・組み込み値、個体ごとの上書きの順で適用する。
`SceneWorld::SetAnimatorParameter`や`SceneEnvironment::SetAnimatorParameter`、Scriptの`context.scene->SetAnimatorParameter`で個体の値を上書きする。
`ClearAnimatorParameter`で上書きを解除する。保存値と実行時上書きはそれぞれ最大64値まで指定でき、実行時の上書きは保存データを変更しない。
Scriptの変更はコールバック後に適用し、オブジェクト削除時に破棄する。上限超過ではScriptの状態とシーンも保持する。
`SceneWorld::AnimatorStatus`で現在の位相・Motion重み・姿勢を確認できる。

`Content/Assets/Scenes/BlendTreePlayground.json`では、左のPlayerが入れ子の1D、中央が2D、右がDirectを使用する。
WASDやアナログ入力で比較する。右のDirectは`MoveRight`・`MoveForward`・`speed`でIdle・Walk・Jumpを混ぜる。
`scripts/GenerateBlendTreePlayground.py`で同じ比較シーンを再生成できる。

`WP1_BLEND_ONLY=1`で回帰テストを実行すると、補間・同期・遷移・循環拒否・保存・Undo/Redo・Prefab・独立複製・App/Editor再生を検証する。
