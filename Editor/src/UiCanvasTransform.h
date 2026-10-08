#pragma once
#include <SceneRuntime/SceneUi.h>
#include <algorithm>
#include <cmath>

namespace Editor
{
    struct UiCanvasTransform
    {
        static SceneRuntime::RectTransformComponent Resize(const SceneRuntime::RectTransformComponent& start,
            float parentRotation,float scale,const std::array<float,2>& handle,const std::array<float,2>& delta)
        {
            auto c=start; const float angle=parentRotation+c.rotation;
            const float x=delta[0]/scale,y=delta[1]/scale;
            const float dx=x*std::cos(angle)+y*std::sin(angle),dy=-x*std::sin(angle)+y*std::cos(angle);
            c.size={std::clamp(c.size[0]+handle[0]*dx,0.0f,100000.0f),std::clamp(c.size[1]+handle[1]*dy,0.0f,100000.0f)};
            const float sx=c.size[0]-start.size[0],sy=c.size[1]-start.size[1];
            c.position[0]+=(handle[0]*sx*std::cos(c.rotation)-handle[1]*sy*std::sin(c.rotation))*0.5f-(0.5f-c.pivot[0])*sx;
            c.position[1]+=(handle[0]*sx*std::sin(c.rotation)+handle[1]*sy*std::cos(c.rotation))*0.5f-(0.5f-c.pivot[1])*sy;
            for (auto& value:c.position) value=std::clamp(value,-100000.0f,100000.0f);
            return c;
        }
        static SceneRuntime::RectTransformComponent Pivot(const SceneRuntime::RectTransformComponent& start,
            const SceneRuntime::UiRect& rect,const std::array<float,2>& delta)
        {
            auto c=start;
            const std::array<float,2> local{delta[0]*std::cos(rect.rotation)+delta[1]*std::sin(rect.rotation),
                -delta[0]*std::sin(rect.rotation)+delta[1]*std::cos(rect.rotation)};
            for (size_t axis=0;axis<2;++axis) if (rect.size[axis]>0) {
                c.pivot[axis]=std::clamp(start.pivot[axis]+local[axis]/rect.size[axis],0.0f,1.0f);
                c.position[axis]=std::clamp(start.position[axis]+(c.pivot[axis]-start.pivot[axis])*rect.size[axis]/rect.scale,-100000.0f,100000.0f);
            }
            return c;
        }
    };
}
