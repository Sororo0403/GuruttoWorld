#include "BlendTreeValidation.h"
using namespace SceneRuntime::Detail;

namespace SceneRuntime
{
    std::vector<Engine::BonePose> BlendTree::SamplePose(const std::vector<AnimatorMotionSample>& samples,
                                                        const Engine::SkeletonData& rig, double phase, bool loop)
    {
        if (!std::isfinite(phase) || phase < 0 || samples.empty() || samples.size() > 256)
            throw std::runtime_error("Invalid Blend Tree phase");
        std::vector<std::vector<Engine::BonePose>> poses;
        std::vector<float> weights;
        double total = 0;
        for (const auto& sample : samples)
        {
            ValidateSample(sample);
            if (sample.weight == 0)
                continue;
            poses.push_back(Engine::Skeleton::Sample(
                rig, sample.clip,
                (loop ? std::fmod(phase, 1.0) : std::min(phase, 1.0)) * ClipDuration(rig, sample.clip), false));
            weights.push_back(sample.weight);
            total += sample.weight;
        }
        if (total <= 0)
            throw std::runtime_error("Empty Blend Tree weights");
        if (poses.size() == 1)
            return poses.front();
        if (poses.size() == 2)
            return Engine::Skeleton::Blend(poses[0], poses[1], static_cast<float>(weights[1] / total));
        auto result = poses.front();
        for (size_t node = 0; node < result.size(); ++node)
        {
            auto& value = result[node];
            value.position = {};
            value.scale = {};
            const auto& first = poses[0][node].rotation;
            auto rotation =
                DirectX::XMQuaternionNormalize(DirectX::XMVectorSet(first[0], first[1], first[2], first[3]));
            double accumulated = weights[0];
            for (size_t index = 0; index < poses.size(); ++index)
            {
                const float weight = static_cast<float>(weights[index] / total);
                const auto& source = poses[index][node];
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    value.position[axis] += source.position[axis] * weight;
                    value.scale[axis] += source.scale[axis] * weight;
                }
                auto quaternion = DirectX::XMQuaternionNormalize(DirectX::XMVectorSet(
                    source.rotation[0], source.rotation[1], source.rotation[2], source.rotation[3]));
                if (index > 0)
                {
                    accumulated += weights[index];
                    rotation = DirectX::XMQuaternionNormalize(DirectX::XMQuaternionSlerp(
                        rotation, quaternion, static_cast<float>(weights[index] / accumulated)));
                }
            }
            DirectX::XMFLOAT4 output;
            DirectX::XMStoreFloat4(&output, DirectX::XMQuaternionNormalize(rotation));
            value.rotation = {output.x, output.y, output.z, output.w};
        }
        return result;
    }
}
