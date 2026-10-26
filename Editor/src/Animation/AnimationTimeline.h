#pragma once
#include "../Editing/EditState.h"
#include <SceneRuntime/Animation/Animation.h>
#include <imgui.h>
#include <cmath>
#include <limits>

namespace Editor
{
    class AnimationTimeline final
    {
    public:
        /// <summary>キーの時刻を移動します。</summary>
        static bool MoveKey(SceneRuntime::AnimationTrack& track, size_t index, float time, float value, size_t channel);
        /// <summary>編集画面を描画します。</summary>
        static bool Draw(EditState& state, SceneRuntime::AnimationTrack& track);

    private:
        struct View
        {
            int channel = 0;
            float duration = 1, low = -1, high = 1;
            std::optional<size_t> key;
        };
        template <class Point>
        static void SelectKey(bool hovered, ImVec2 mouse, const SceneRuntime::AnimationTrack& track, View& view,
                              const Point& point)
        {
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                view.key.reset();
                float nearest = 100;
                for (size_t index = 0; index < track.keys.size(); ++index)
                {
                    const auto p = point(track.keys[index].time, track.keys[index].value[view.channel]);
                    const float distance = (p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y);
                    if (distance < nearest)
                    {
                        view.key = index;
                        nearest = distance;
                    }
                }
            }
        }
        inline static std::map<ImGuiID, View> views_;
    };
}
