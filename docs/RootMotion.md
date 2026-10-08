# ルートモーション

## 抽出計算

`Engine::RootMotion::Sample(rig,bone,clip,phase,loop)`は、指定ボーンのローカル位置と回転を剛体行列として返す。phaseはクリップ長で正規化した時刻であり、Scaleは抽出しない。空のクリップ名はRest姿勢を返す。global=trueでは祖先の位置・回転を含むSkeleton座標の剛体フレームを評価し、祖先Scaleによる位置の変化も反映する。PoseFrameは評価済みの姿勢から同じフレームを取得する。

ループしない場合は終端で固定する。ループする場合は開始と終端の差を次の周回に引き継ぐ。開始行列をS、終端をE、周回内の行列をP、完了周回数をnとすると、行ベクトル規約で`P × (inverse(S) × E)^n`となる。位置の戻りを防ぎ、回転するクリップは次の周回の移動方向も回転する。開始位置や回転がIdentityでなくても扱う。

累積は二分累乗で計算する。剛体の位置・Quaternionとして合成し、Quaternionを正規化するため、多数の周回で回転行列の丸め誤差がScaleへ蓄積しない。

`Delta(rig,bone,clip,from,to,loop)`は、行ベクトル規約で`Delta × Sample(from) = Sample(to)`となる変換を返す。周回内の姿勢と区間で跨いだ周回数だけから計算し、大きな累積位置の差を取らないため、長時間再生後の小さな移動も保持する。同じ時刻の差はIdentityである。不正なボーン・クリップ・Quaternion・時刻、逆向きの区間、非有限の計算結果を拒否する。周回数の整数精度を保つため、phaseは0以上2の53乗未満とする。

## 検証と接続状況

`WP1_ROOT_MOTION_ONLY=1`で、ループ境界・複数周回・回転する移動・非ループ終端・差分による姿勢再構築・100万周回の剛体性・不正入力を検証する。通常のAnimator検証にも含める。

Animatorの`rootMotion`と`rootBone`に、抽出の有効状態とボーン名を保存する。既存シーンは省略でき、既定は無効。名前は一意に解決できるボーンを指定する。実行状態の`rootDelta`に、その更新区間のSkeleton座標での位置・回転の差分を出力する。Scaleは常に1である。

Blend Treeは同じ正規化位相の各クリップの差分を重み付きで混ぜる。遷移時には直前状態のモーション・位相・速度・ループ設定を保持して位相を進め、前後の状態の差分をフェード区間の平均重みで補間する。Root Motionの前状態は進行し、既存のボーン姿勢のフェード元は引き続きスナップショットである。

指定ボーンのグローバル位置・回転がRestになる共通の剛体変換を、Skeletonの最上位ノードへ適用してからIKを評価する。アニメーションする祖先も含めて移動を取り除き、ボーンのScaleと相対的な変形を保持する。`basePose`にも抽出後・IK前の姿勢を保持する。無効なAnimatorは差分をIdentityにし、姿勢と時計を保持する。抽出に失敗した場合は実行状態を変更しない。

SceneWorldはRestフレーム・inverseRoot・importScaleを使って差分をモデル座標へ変換し、ObjectのローカルTransformへ適用する。親子Transformを解決した後、移動後のWorld座標でIKと描画姿勢を再評価する。更新中は候補状態だけを変更し、いずれかの個体で失敗すれば全個体のTransform・時計・Script状態を保持する。Scale・回転・位置で表現できないShearが生じる組み合わせは更新を拒否する。微小な移動も適用する。

`RootMotionPlayground.json`はその場再生・直進・回転移動を比較する。`scripts/GenerateRootMotionPlayground.py`でモデルとシーンを再生成できる。`WP1_ROOT_PLAYBACK_ONLY=1`はObject移動、ループ、親Transform、移動後のWorld IK、失敗時の保持、App／EditorのPlay・Pause・Step・Stopを検証する。回転移動のGPU画像を、移動を抽出しない元の姿勢の描画と比較する。祖先アニメーションの変換再構築も数学検証に含める。

物理との衝突・移動制御、Scriptのルートモーション制御、Inspector編集は引き続き実装する。ルートモーション全体はまだ完了扱いにしない。
