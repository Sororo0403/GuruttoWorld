#pragma once
#include <array>
#include <string>
#include <filesystem>
#include <vector>
namespace SceneRuntime {
struct AudioSourceComponent {
    std::string id="audio"; bool enabled=true;
    std::filesystem::path clip;
    float volume=1; bool loop=false,playOnAwake=false;
    std::string cue,volumeBinding,bus="Master";
    bool spatial=false,streaming=false;
    float minimumDistance=1,maximumDistance=100,spatialBlend=1,pitch=1,lowPass=1;
    bool operator==(const AudioSourceComponent&) const = default;
};
struct AudioListenerComponent {
    std::string id="listener"; bool enabled=true; float volume=1;
    bool operator==(const AudioListenerComponent&) const = default;
};
struct AudioMixerGroup {
    std::string name="Master",volumeBinding;
    float volume=1,lowPass=1,reverb=0; bool mute=false;
    bool operator==(const AudioMixerGroup&) const = default;
};
struct AudioMixerComponent {
    std::string id="mixer"; bool enabled=true;
    std::vector<AudioMixerGroup> groups{{}};
    bool operator==(const AudioMixerComponent&) const = default;
};
}
