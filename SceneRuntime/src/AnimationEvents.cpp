#include <SceneRuntime/AnimationEvents.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
    double Duration(const Engine::SkeletonData& rig,const std::string& name)
    {
        const auto found=std::ranges::find(rig.clips,name,&Engine::SkeletalClip::name);
        if (found==rig.clips.end() || !std::isfinite(found->duration) || found->duration<=0) throw std::runtime_error("Animation event clip missing or invalid: "+name);
        return found->duration;
    }
    bool Text(const std::string& value,size_t maximum,bool empty=false)
    {
        return (empty || !value.empty()) && value.size()<=maximum && value.find('\0')==std::string::npos;
    }
}
namespace SceneRuntime
{
    void AnimationEvents::Validate(const std::vector<AnimatorEventKey>& keys,const Engine::SkeletonData* rig)
    {
        if (keys.size()>256) throw std::runtime_error("Too many animation event keys");
        for (const auto& key : keys)
        {
            if (!Text(key.clip,256) || !Text(key.name,128) || !Text(key.stringValue,1024,true) ||
                !std::isfinite(key.time) || key.time<0 || key.time>1000000 || !std::isfinite(key.value) || std::abs(key.value)>1000000 ||
                !std::isfinite(key.minimumWeight) || key.minimumWeight<0 || key.minimumWeight>1) throw std::runtime_error("Invalid animation event key");
            if (rig && key.time>Duration(*rig,key.clip)) throw std::runtime_error("Animation event is past clip end");
        }
    }
    std::vector<AnimatorEventOccurrence> AnimationEvents::Collect(const std::vector<AnimatorEventKey>& keys,const Engine::SkeletonData& rig,
        const std::string& state,const std::vector<AnimatorMotionSample>& motions,double from,double to,bool loop,bool atStart,float fromBlendWeight,float toBlendWeight)
    {
        Validate(keys,&rig);
        if (!std::isfinite(from) || !std::isfinite(to) || from<0 || to<from || !std::isfinite(fromBlendWeight) || !std::isfinite(toBlendWeight) ||
            fromBlendWeight<0 || fromBlendWeight>1 || toBlendWeight<0 || toBlendWeight>1) throw std::runtime_error("Invalid animation event interval");
        std::vector<AnimatorEventOccurrence> events;
        if (to==from || keys.empty()) return events;
        std::map<std::string,double> weights;
        for (const auto& motion : motions)
        {
            if (!std::isfinite(motion.weight) || motion.weight<0 || motion.weight>1) throw std::runtime_error("Invalid event motion weight");
            if (!motion.clip.empty() && motion.weight>0) weights[motion.clip]+=motion.weight;
        }
        for (size_t index=0;index<keys.size();++index)
        {
            const auto& key=keys[index]; const auto active=weights.find(key.clip); if (active==weights.end()) continue;
            const double clipWeight=std::min(active->second,1.0);
            const double minimum=key.minimumWeight==0 ? 0 : (static_cast<double>(key.minimumWeight)+std::nextafter(key.minimumWeight,-(std::numeric_limits<float>::infinity)()))*.5;
            if (clipWeight*std::max(fromBlendWeight,toBlendWeight)<minimum) continue;
            const double duration=Duration(rig,key.clip),position=key.time/duration;
            // floatのキー時刻と累積時計の丸め差を吸収します。両端へ同じ許容値を加え、次の区間で再通知しません。
            const double epsilon=std::min(1e-7,1e-6/duration);
            const bool start=atStart && from==0;
            double first=loop ? std::max(0.0,start ? 0.0 : std::floor(from+epsilon-position)+1) : 0;
            double last=loop ? std::floor(to+epsilon-position) : 0;
            if (last<first || (!loop && ((!start && position<=from+epsilon) || position>to+epsilon))) continue;
            const double blendDelta=static_cast<double>(toBlendWeight)-fromBlendWeight;
            if (blendDelta!=0)
            {
                const double progress=(minimum/clipWeight-fromBlendWeight)/blendDelta;
                const double crossing=from+progress*(to-from)-position;
                if (blendDelta>0 && progress>0) first=std::max(first,std::ceil(crossing));
                if (blendDelta<0 && progress<1) last=std::min(last,std::floor(crossing));
            }
            if (last<first) continue;
            if (last>=9007199254740992.0 || last-first+1>static_cast<double>(Limit+2)) throw std::runtime_error("Animation event cycle count exceeds limit");
            for (double cycle=first;cycle<=last;++cycle)
            {
                const double phase=cycle+position;
                const double progress=std::clamp((phase-from)/(to-from),0.0,1.0);
                const float weight=static_cast<float>(clipWeight*(fromBlendWeight+blendDelta*progress));
                if (weight<key.minimumWeight) continue;
                if (events.size()>=Limit) throw std::runtime_error("Animation event count exceeds limit");
                events.push_back({key.clip,state,key.name,key.time,key.value,weight,key.stringValue,key.intValue,static_cast<std::uint64_t>(cycle),phase,index});
            }
        }
        std::ranges::stable_sort(events,[](const auto& a,const auto& b) {
            if (a.phase!=b.phase) return a.phase<b.phase;
            if ((a.time==0)!=(b.time==0)) return a.time!=0;
            return a.keyIndex<b.keyIndex;
        });
        return events;
    }
    void AnimationEvents::ValidateOccurrence(const AnimatorEventOccurrence& event)
    {
        if (!Text(event.clip,256) || !Text(event.state,128) || !Text(event.name,128) || !Text(event.stringValue,1024,true) ||
            !std::isfinite(event.time) || event.time<0 || event.time>1000000 || !std::isfinite(event.value) || std::abs(event.value)>1000000 ||
            !std::isfinite(event.weight) || event.weight<0 || event.weight>1 || !std::isfinite(event.phase) || event.phase<0 ||
            event.cycle>=9007199254740992ULL || event.keyIndex>=256 || event.phase<static_cast<double>(event.cycle) || event.phase>static_cast<double>(event.cycle)+1)
            throw std::runtime_error("Invalid animation event metadata");
    }
}
