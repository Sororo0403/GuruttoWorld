#pragma once
#include <array>
#include <string>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace SceneRuntime
{
    struct JointComponent
    {
        std::string id="joint",type="fixed",target;
        bool enabled=true,collideConnected=false;
        std::array<float,3> anchor{},connectedAnchor{},axis{0,0,1};
        float minimum=-90,maximum=90,swing=45,minDistance=0,maxDistance=1;
        bool operator==(const JointComponent&) const = default;
        /// <summary>有限なアンカー、軸、距離と角度制限を検証します。</summary>
        void Validate() const
        {
            if(type!="fixed" && type!="hinge" && type!="distance" && type!="swingTwist") throw std::runtime_error("Unknown Joint type");
            for(const auto& vector:{anchor,connectedAnchor,axis})
                if(std::any_of(vector.begin(),vector.end(),[](float v){return !std::isfinite(v) || std::abs(v)>100000;})) throw std::runtime_error("Invalid Joint vector");
            const float length=axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2];
            if(length<0.000001f || !std::isfinite(length)) throw std::runtime_error("Joint axis cannot be zero");
            ValidateLimits();
        }
    private:
        /// <summary>角度と距離の制限値を検証します。</summary>
        void ValidateLimits() const
        {
            const auto range=[](float value,float low,float high){return std::isfinite(value) && value>=low && value<=high;};
            if(!range(minimum,-180,0) || !range(maximum,0,180) || !range(swing,0,180)) throw std::runtime_error("Invalid Joint angle limits");
            if(!range(minDistance,0,100000) || !range(maxDistance,minDistance,100000)) throw std::runtime_error("Invalid Joint distance limits");
        }
    };
}
