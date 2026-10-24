// 製品の実装を直接コンパイルし、実際に生成する音声をテストで検査します。
#include "../../../Games/GuruttoWorld/Assets/Scripts/SunriseStreet.cpp"
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include "../../../Editor/src/Playback/GameSession.h"
#include <stdexcept>

// 実際の音声プールで開始失敗・停止・コマ送り・フォーカス変更を検証します。
void ValidateSunriseSound()
{
    using namespace SceneRuntime;
    const auto require = [](bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    };
    ScenePlacement sound;
    sound.id = "renamed-sound";
    ScriptComponent script;
    script.behaviour = "SunriseSound";
    script.parameters["volume"] = 0;
    sound.scripts = {script};
    ScriptDefinition failure;
    failure.update = [](ScriptContext&)
    {
        throw std::runtime_error("expected failed sound frame");
    };
    require(ScriptRegistry::Register("ValidationSoundFailure", failure), "sound failure definition");
    ScenePlacement broken;
    broken.id = "broken";
    broken.scripts.push_back({"script", true, "ValidationSoundFailure", {}});
    SceneLayout layout;
    layout.objects = {sound, broken};
    std::string error;
    ScriptRuntime runtime;
    require(!runtime.Update(layout, .016, error) && bank.users == 0 && !bank.ready,
            "failed sound start releases actual audio bank");
    layout.objects = {sound};
    require(runtime.Update(layout, .016, error), "sound starts before editing its clip");
    layout.objects[0].scripts[0].data["clip"].value = std::string("Collect");
    ScriptEvent play;
    play.name = "sunrise:play";
    play.target = sound.id;
    runtime.QueueEvent(play);
    require(runtime.Update(layout, .016, error), "editing a live sound clip does not throw on its first event");
    require(bank.handles.contains("Collect") && runtime.TakeUiCommands().values.at(sound.id) == 1,
            "edited sound clip is loaded and played");
    runtime.Stop(layout);
    Engine::Window window;
    Engine::DirectX12Renderer renderer;
    require(window.Create(L"Sound lifecycle validation", 64, 64) && renderer.Initialize(window.GetHandle()),
            "sound test renderer");
    layout.objects = {sound};
    Editor::GameSession session;
    const auto root = std::filesystem::absolute("Games/GuruttoWorld");
    require(session.Play(renderer, root, layout, error), "sound session starts");
    session.Update(.016, true);
    require(bank.users == 1 && bank.ready && bank.audio.OutputVolume() > 0, "renamed sound initializes independently");
    require(session.Pause() && bank.audio.OutputVolume() == 0, "Pause silences actual custom sound output");
    require(session.Step() && bank.audio.OutputVolume() == 0, "Step keeps sound paused after advancing");
    require(session.Play(renderer, root, layout, error) && bank.audio.OutputVolume() > 0,
            "Play resumes custom sound output");
    session.Update(.016, false);
    require(bank.audio.OutputVolume() == 0, "focus loss silences custom output without a script tick");
    session.Update(.016, true);
    require(bank.audio.OutputVolume() > 0, "focus return restores custom output");
    require(session.Stop() && bank.users == 0 && !bank.ready, "Stop releases custom sound output");
    require(renderer.WaitForIdle(), "sound test completion");
}
