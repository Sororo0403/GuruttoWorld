# 音響コンポーネント

Editor の Inspector > Component 追加から AudioSource、AudioListener、AudioMixer を配置できます。設定はシーン、Prefab、複製、Undo の Component 経路を共用します。

- AudioSource のクリップは Assets 内の WAV、MP3、AAC などです。通常再生は PCM をキャッシュし、Streaming は Media Foundation のデコーダーから最大 3 チャンクを逐次補充します。1 チャンクは最大 4 MiB で、30 秒以上のクリップも全展開せず再生します。Streaming の再生中は元ファイルを保持するため、元ファイルを移動・削除する前に停止とアンロードを行います。
- Spatial を有効にすると、階層のワールド座標とリスナーの向きから X3DAudio が左右の出力行列を計算します。Minimum Distance までは減衰せず、Maximum Distance で無音になる距離曲線を使います。Spatial Blend は 2D と 3D の混合率、Pitch は 0.25〜2 倍です。
- AudioListener が複数あるときはシーン順の最初の有効なリスナーを使います。未配置時は Main Camera、カメラもない場合は原点・前方 +Z を使います。Volume は全体の出力音量です。
- AudioMixer の各グループは XAudio2 の独立した submix ボイスです。AudioSource の Bus で接続します。Master グループへすべてのグループを接続し、Master の Volume、Mute、Low Pass、Reverb が全体に適用されます。グループ側の同じ設定も重ねて反映されます。Volume Binding は Canvas の状態値との乗算です。
- Low Pass は XAudio2 の低域フィルタの正規化周波数（0〜1）、Reverb は Room プリセットの wet/dry 混合率です。
- Play on Awake、Cue、UI ボタン、アニメーションの音声イベントは通常クリップと Streaming の同じ再生経路を使います。Play の一時停止・フォーカス喪失はキューと再生位置を保持し、Stop はボイスとストリーム送信済みチャンクを解放します。

音響の出力はステレオ submix から既定の Windows 出力デバイスへ接続します。デバイスがサラウンドの場合の最終マッピングは XAudio2 が行います。

`WP1_AUDIO_FEATURES_ONLY=1` で ReviewRegressionValidation を実行すると、保存互換、3D 行列、距離減衰、ミキサーの実際のボイス/エフェクト作成、Streaming の保持メモリ・補充・停止・再開、親の移動を含む SceneAudio の回帰を実行します。
