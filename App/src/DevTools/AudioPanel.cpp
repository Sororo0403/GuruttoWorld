#include "AudioPanel.h"
#include <imgui.h>

namespace App
{
    void AudioPanel::Draw(Engine::AudioSystem& audio, Engine::SoundHandle sound)
    {
        ImGui::SetNextWindowPos(ImVec2(900.0f, 230.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(290.0f, 170.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Audio"))
        {
            static bool loop = false;
            float volume = audio.GetVolume(sound);
            ImGui::TextUnformatted(sound == 0 ? "Audio unavailable" : "Sample.wav");
            ImGui::BeginDisabled(sound == 0);
            ImGui::Checkbox("Loop (next play)", &loop);
            if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f)) audio.SetVolume(sound, volume);
            if (ImGui::Button("Play")) audio.Play(sound, loop);
            ImGui::SameLine();
            if (ImGui::Button("Stop")) audio.Stop(sound);
            ImGui::TextUnformatted(audio.IsPlaying(sound) ? "Playing" : "Stopped");
            ImGui::EndDisabled();
        }
        ImGui::End();
    }
}
