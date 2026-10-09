#pragma once
#include <SceneRuntime/SceneUi.h>
#include <Engine/Audio/AudioSystem.h>
#include <set>
namespace SceneRuntime {
class SceneAudio final {
public:
    /// <summary>シーンの音源とミキサーを初期化します。</summary>
    bool Initialize(const std::filesystem::path& root,const SceneLayout& layout,std::string& error);
    /// <summary>音源の追加・削除・クリップやストリーム方式の変更を反映します。</summary>
    bool Refresh(const SceneLayout& layout,std::string& error);
    /// <summary>同じContentでは存続音源の再生位置を保持し、新規・削除音源だけ反映します。</summary>
    bool Reconcile(const std::filesystem::path& root,const SceneLayout& layout,std::string& error) {return root_==root?Refresh(layout,error):Initialize(root,layout,error);}
    /// <summary>ワールド座標とリスナー、音量バインディング、ミキサー、ストリームを更新します。</summary>
    void Update(const SceneLayout& layout,const UiState& state,bool active=true);
    /// <summary>オブジェクトの音源を先頭から再生します。</summary>
    bool Play(const std::string& object);
    /// <summary>音源が再生中か取得します。</summary>
    bool IsPlaying(const std::string& object) const;
    /// <summary>音源の現在の音量を取得します。</summary>
    float Volume(const std::string& object) const;
    /// <summary>実際の3D出力行列を取得します。</summary>
    std::vector<float> OutputMatrix(const std::string& object) const;
    /// <summary>ストリームの保持PCMバイト数を取得します。</summary>
    size_t BufferedBytes(const std::string& object) const;
    /// <summary>キュー名に対応する音源を再生します。</summary>
    void Cue(const std::string& cue);
    /// <summary>全音源を一時停止または再開します。</summary>
    void Pause(bool paused);
    /// <summary>全音源の再生を停止します。</summary>
    void Stop();
private:
    struct Source {Engine::SoundHandle handle=0; bool loop=false,awake=false,started=false,streaming=false; std::filesystem::path clip;};
    /// <summary>各音響グループの実際のバス設定を更新します。</summary>
    bool Mix(const SceneLayout& layout,const UiState& state);
    /// <summary>階層を含めた音源とリスナーの位置を更新します。</summary>
    void Spatial(const SceneLayout& layout);
    /// <summary>既存音源を再利用するか新しいクリップを開きます。</summary>
    bool PrepareSource(const ScenePlacement& placement,std::map<std::string,Source>& pending,std::vector<Engine::SoundHandle>& loaded);
    bool paused_=false,initialized_=false;
    std::filesystem::path root_;
    Engine::AudioSystem audio_;
    std::map<std::string,Source> sources_;
    std::map<std::string,std::string> cues_;
    std::set<std::string> configuredBuses_;
};
}
