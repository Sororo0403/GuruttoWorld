#include <SceneRuntime/BlendTree.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <set>
#include <stdexcept>

namespace
{
    bool Identifier(const std::string& value,size_t limit=128)
    {
        return !value.empty() && value.size()<=limit && value.find('\0')==std::string::npos;
    }
    double ClipDuration(const Engine::SkeletonData& rig,const std::string& name)
    {
        if (name.empty()) return 1;
        const auto found=std::ranges::find(rig.clips,name,&Engine::SkeletalClip::name);
        if (found==rig.clips.end() || !std::isfinite(found->duration) || found->duration<=0) throw std::runtime_error("Blend Tree clip missing or invalid: "+name);
        return found->duration;
    }
    double Parameter(const std::map<std::string,float>& parameters,const std::string& name)
    {
        const auto found=parameters.find(name); const double value=found==parameters.end() ? 0 : found->second;
        if (!std::isfinite(value)) throw std::runtime_error("Nonfinite Blend Tree parameter: "+name);
        return value;
    }
    const SceneRuntime::AnimatorBlendTree& Tree(const std::vector<SceneRuntime::AnimatorBlendTree>& trees,const std::string& name)
    {
        const auto found=std::ranges::find(trees,name,&SceneRuntime::AnimatorBlendTree::name);
        if (found==trees.end()) throw std::runtime_error("Blend Tree missing: "+name);
        return *found;
    }
    std::vector<double> Weights(const SceneRuntime::AnimatorBlendTree& tree,const std::map<std::string,float>& parameters)
    {
        using SceneRuntime::AnimatorBlendType;
        std::vector<double> weights(tree.children.size());
        if (tree.type==AnimatorBlendType::Direct)
        {
            for (size_t index=0;index<weights.size();++index) weights[index]=std::max(0.0,Parameter(parameters,tree.children[index].parameter));
        }
        else if (tree.type==AnimatorBlendType::OneDimensional)
        {
            const auto value=Parameter(parameters,tree.parameterX);
            std::vector<size_t> order(weights.size()); std::iota(order.begin(),order.end(),0);
            std::ranges::sort(order,[&](size_t a,size_t b) { return tree.children[a].threshold<tree.children[b].threshold; });
            if (value<=tree.children[order.front()].threshold) weights[order.front()]=1;
            else if (value>=tree.children[order.back()].threshold) weights[order.back()]=1;
            else for (size_t index=1;index<order.size();++index)
            {
                const double next=tree.children[order[index]].threshold,previous=tree.children[order[index-1]].threshold;
                if (value>next) continue;
                weights[order[index]]=(value-previous)/(next-previous); weights[order[index-1]]=1-weights[order[index]]; break;
            }
        }
        else
        {
            const double x=Parameter(parameters,tree.parameterX),y=Parameter(parameters,tree.parameterY);
            // 座標間に線形な重みの帯を作り、各子の最も小さい帯を正規化します。
            for (size_t index=0;index<weights.size();++index)
            {
                const auto& point=tree.children[index].position; double weight=1;
                for (size_t other=0;other<weights.size();++other)
                {
                    if (index==other) continue;
                    const auto& target=tree.children[other].position;
                    const double dx=static_cast<double>(target[0])-point[0],dy=static_cast<double>(target[1])-point[1];
                    weight=std::min(weight,std::clamp(1-((x-point[0])*dx+(y-point[1])*dy)/(dx*dx+dy*dy),0.0,1.0));
                }
                weights[index]=weight;
            }
        }
        const double total=std::accumulate(weights.begin(),weights.end(),0.0);
        if (total>0) std::ranges::transform(weights,weights.begin(),[&](double weight) { return weight/total; });
        return weights;
    }
    void Expand(const std::vector<SceneRuntime::AnimatorBlendTree>& trees,const std::string& name,
        const std::map<std::string,float>& parameters,double weight,double speed,std::vector<SceneRuntime::AnimatorMotionSample>& samples)
    {
        const auto& tree=Tree(trees,name); const auto weights=Weights(tree,parameters);
        if (std::none_of(weights.begin(),weights.end(),[](double value) { return value>0; }))
        {
            samples.push_back({"",speed,static_cast<float>(weight)}); return;
        }
        for (size_t index=0;index<weights.size();++index)
        {
            if (weights[index]<=0) continue;
            const auto& child=tree.children[index];
            if (!child.blendTree.empty()) Expand(trees,child.blendTree,parameters,weight*weights[index],speed*child.speed,samples);
            else samples.push_back({child.clip,speed*child.speed,static_cast<float>(weight*weights[index])});
        }
    }
    void ValidateSample(const SceneRuntime::AnimatorMotionSample& sample)
    {
        if (!std::isfinite(sample.weight) || sample.weight<0 || !std::isfinite(sample.speed) || sample.speed<=0) throw std::runtime_error("Invalid Blend Tree sample");
    }
}
namespace SceneRuntime
{
    void BlendTree::Validate(const std::vector<AnimatorBlendTree>& trees,const Engine::SkeletonData* rig)
    {
        if (trees.size()>32) throw std::runtime_error("Too many Blend Trees");
        std::set<std::string> names;
        for (const auto& tree : trees)
        {
            if (!Identifier(tree.name) || !names.insert(tree.name).second || tree.children.empty() || tree.children.size()>32 ||
                (tree.type!=AnimatorBlendType::OneDimensional && tree.type!=AnimatorBlendType::Cartesian2D && tree.type!=AnimatorBlendType::Direct)) throw std::runtime_error("Invalid Blend Tree");
            if (tree.type!=AnimatorBlendType::Direct && !Identifier(tree.parameterX)) throw std::runtime_error("Blend Tree parameter missing");
            if (tree.type==AnimatorBlendType::Cartesian2D && !Identifier(tree.parameterY)) throw std::runtime_error("2D Blend Tree parameter missing");
            std::set<float> thresholds; std::set<std::array<float,2>> points;
            for (const auto& child : tree.children)
            {
                if (child.clip.size()>256 || child.clip.find('\0')!=std::string::npos || child.blendTree.size()>128 || child.blendTree.find('\0')!=std::string::npos ||
                    (!child.clip.empty() && !child.blendTree.empty()) || !std::isfinite(child.speed) || child.speed<.001f || child.speed>1000 ||
                    !std::isfinite(child.threshold) || std::abs(child.threshold)>1000000 ||
                    std::ranges::any_of(child.position,[](float value) { return !std::isfinite(value) || std::abs(value)>1000000; })) throw std::runtime_error("Invalid Blend Tree motion");
                if (tree.type==AnimatorBlendType::OneDimensional && !thresholds.insert(child.threshold).second) throw std::runtime_error("Duplicate Blend Tree threshold");
                if (tree.type==AnimatorBlendType::Cartesian2D && !points.insert(child.position).second) throw std::runtime_error("Duplicate Blend Tree position");
                if (tree.type==AnimatorBlendType::Direct && !Identifier(child.parameter)) throw std::runtime_error("Direct Blend Tree parameter missing");
                if (rig && child.blendTree.empty()) ClipDuration(*rig,child.clip);
            }
        }
        std::map<std::string,std::pair<size_t,size_t>> dimensions; std::set<std::string> visiting;
        std::function<std::pair<size_t,size_t>(const std::string&)> visit=[&](const std::string& name)
        {
            if (const auto found=dimensions.find(name);found!=dimensions.end()) return found->second;
            if (!visiting.insert(name).second) throw std::runtime_error("Cyclic Blend Tree reference");
            size_t depth=1,leaves=0;
            for (const auto& child : Tree(trees,name).children)
            {
                if (child.blendTree.empty()) ++leaves;
                else { const auto nested=visit(child.blendTree); depth=std::max(depth,nested.first+1); leaves+=nested.second; }
                if (depth>8 || leaves>256) throw std::runtime_error("Blend Tree expansion exceeds limit");
            }
            visiting.erase(name); const auto result=std::pair{depth,leaves}; dimensions.emplace(name,result); return result;
        };
        for (const auto& tree : trees) visit(tree.name);
    }
    std::vector<AnimatorMotionSample> BlendTree::Samples(const std::vector<AnimatorBlendTree>& trees,const std::string& name,const std::map<std::string,float>& parameters)
    {
        Validate(trees);
        std::vector<AnimatorMotionSample> samples; Expand(trees,name,parameters,1,1,samples);
        const double total=std::accumulate(samples.begin(),samples.end(),0.0,[](double value,const auto& sample) { return value+sample.weight; });
        if (total<=0) return {{"",1,1}};
        for (auto& sample : samples) sample.weight=static_cast<float>(sample.weight/total);
        return samples;
    }
    double BlendTree::Duration(const std::vector<AnimatorMotionSample>& samples,const Engine::SkeletonData& rig)
    {
        double duration=0,total=0;
        for (const auto& sample : samples)
        {
            ValidateSample(sample); if (sample.weight==0) continue;
            duration+=sample.weight*ClipDuration(rig,sample.clip)/sample.speed; total+=sample.weight;
        }
        if (total<=0 || !std::isfinite(duration) || duration<=0) throw std::runtime_error("Invalid Blend Tree duration");
        return duration/total;
    }
    std::vector<Engine::BonePose> BlendTree::SamplePose(const std::vector<AnimatorMotionSample>& samples,const Engine::SkeletonData& rig,double phase,bool loop)
    {
        if (!std::isfinite(phase) || phase<0 || samples.empty() || samples.size()>256) throw std::runtime_error("Invalid Blend Tree phase");
        std::vector<std::vector<Engine::BonePose>> poses; std::vector<float> weights;
        double total=0;
        for (const auto& sample : samples)
        {
            ValidateSample(sample); if (sample.weight==0) continue;
            poses.push_back(Engine::Skeleton::Sample(rig,sample.clip,(loop ? std::fmod(phase,1.0) : std::min(phase,1.0))*ClipDuration(rig,sample.clip),false));
            weights.push_back(sample.weight); total+=sample.weight;
        }
        if (total<=0) throw std::runtime_error("Empty Blend Tree weights");
        if (poses.size()==1) return poses.front();
        if (poses.size()==2) return Engine::Skeleton::Blend(poses[0],poses[1],static_cast<float>(weights[1]/total));
        auto result=poses.front();
        for (size_t node=0;node<result.size();++node)
        {
            auto& value=result[node]; value.position={}; value.scale={};
            const auto& first=poses[0][node].rotation;
            auto rotation=DirectX::XMQuaternionNormalize(DirectX::XMVectorSet(first[0],first[1],first[2],first[3]));
            double accumulated=weights[0];
            for (size_t index=0;index<poses.size();++index)
            {
                const float weight=static_cast<float>(weights[index]/total); const auto& source=poses[index][node];
                for (size_t axis=0;axis<3;++axis) { value.position[axis]+=source.position[axis]*weight; value.scale[axis]+=source.scale[axis]*weight; }
                auto quaternion=DirectX::XMQuaternionNormalize(DirectX::XMVectorSet(source.rotation[0],source.rotation[1],source.rotation[2],source.rotation[3]));
                if (index>0)
                {
                    accumulated+=weights[index];
                    rotation=DirectX::XMQuaternionNormalize(DirectX::XMQuaternionSlerp(rotation,quaternion,static_cast<float>(weights[index]/accumulated)));
                }
            }
            DirectX::XMFLOAT4 output; DirectX::XMStoreFloat4(&output,DirectX::XMQuaternionNormalize(rotation));
            value.rotation={output.x,output.y,output.z,output.w};
        }
        return result;
    }
}
