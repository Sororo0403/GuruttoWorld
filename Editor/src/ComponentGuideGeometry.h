#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <vector>
#include <cmath>
#include <numbers>
#include <algorithm>

namespace Editor {
struct ComponentGuideGeometry {
    using Point=std::array<float,3>;
    using Segment=std::array<Point,2>;
    using Lines=std::vector<Segment>;
    static Lines Box(Point center,Point half) {
        Lines result;
        for(int i=0;i<8;++i) for(int axis=0;axis<3;++axis) if(!(i&(1<<axis))) {
            Point a,b;for(int j=0;j<3;++j) {a[j]=center[j]+((i&(1<<j))?half[j]:-half[j]);b[j]=a[j];}
            b[axis]+=2*half[axis];result.push_back({a,b});
        } return result;
    }
    static Lines Sphere(float radius,Point center={}) {
        Lines result;
        for(int axis=0;axis<3;++axis) for(int i=0;i<32;++i) {
            Point a=center,b=center;
            const float first=i*2*std::numbers::pi_v<float>/32,last=(i+1)*2*std::numbers::pi_v<float>/32;
            a[(axis+1)%3]+=radius*std::cos(first);a[(axis+2)%3]+=radius*std::sin(first);
            b[(axis+1)%3]+=radius*std::cos(last);b[(axis+2)%3]+=radius*std::sin(last);
            result.push_back({a,b});
        } return result;
    }
    static Lines Collider(const SceneRuntime::BoxColliderComponent& c,Point scale={1,1,1}) {
        for(auto& value:scale) value=std::abs(value);
        if(std::any_of(scale.begin(),scale.end(),[](float value){return !std::isfinite(value) || value<=0;})) return {};
        Lines result;
        if(c.shape=="box") return Box(c.center,{c.size[0]*.5f,c.size[1]*.5f,c.size[2]*.5f});
        if(c.shape=="sphere" || (c.shape=="capsule" && c.halfHeight==0)) result=Sphere(c.radius*std::max({scale[0],scale[1],scale[2]}));
        else if(c.shape=="capsule") {
            const float radius=c.radius*std::max(scale[0],scale[2]),height=c.halfHeight*scale[1];
            for(int i=0;i<32;++i) {
                const float first=i*2*std::numbers::pi_v<float>/32,last=(i+1)*2*std::numbers::pi_v<float>/32;
                for(float sign:{-1.0f,1.0f}) result.push_back({Point{radius*std::cos(first),sign*height,radius*std::sin(first)},Point{radius*std::cos(last),sign*height,radius*std::sin(last)}});
                if(i%8==0) result.push_back({Point{radius*std::cos(first),-height,radius*std::sin(first)},Point{radius*std::cos(first),height,radius*std::sin(first)}});
            }
            for(int plane=0;plane<2;++plane) for(float sign:{-1.0f,1.0f}) for(int i=0;i<16;++i) {
                const float a=i*std::numbers::pi_v<float>/16,b=(i+1)*std::numbers::pi_v<float>/16;
                Point first{},last{};first[plane*2]=radius*std::cos(a);last[plane*2]=radius*std::cos(b);
                first[1]=sign*(height+radius*std::sin(a));last[1]=sign*(height+radius*std::sin(b));result.push_back({first,last});
            }
        }
        for(auto& segment:result) for(auto& point:segment) for(size_t axis=0;axis<3;++axis) point[axis]=c.center[axis]+point[axis]/scale[axis];
        return result;
    }
    static Lines Camera(const SceneRuntime::CameraComponent& c) {
        const float tangent=std::tan(c.verticalFov*std::numbers::pi_v<float>/360);
        const auto ring=[&](float z){const float y=z*tangent,x=y*c.referenceAspect;return std::array<Point,4>{{{-x,-y,z},{x,-y,z},{x,y,z},{-x,y,z}}};};
        const auto nearRing=ring(c.nearClip),farRing=ring(c.farClip);Lines result;
        for(int i=0;i<4;++i) {const int next=(i+1)%4;result.push_back({nearRing[i],nearRing[next]});result.push_back({farRing[i],farRing[next]});result.push_back({nearRing[i],farRing[i]});}
        return result;
    }
    static Lines Spot(const SceneRuntime::SpotLightComponent& c) {
        // Light attenuation is spherical; cone boundary ends on the range sphere.
        Lines result;
        for(float angle:{c.innerAngle,c.outerAngle}) {
            const float radians=angle*std::numbers::pi_v<float>/360;
            const float radius=c.range*std::sin(radians),z=c.range*std::cos(radians);
            for(int i=0;i<32;++i) {
                const float first=i*2*std::numbers::pi_v<float>/32,last=(i+1)*2*std::numbers::pi_v<float>/32;
                const Point a{radius*std::cos(first),radius*std::sin(first),z},b{radius*std::cos(last),radius*std::sin(last),z};
                result.push_back({a,b});if(i%8==0) result.push_back({Point{},a});
            }
        } return result;
    }
};
}
