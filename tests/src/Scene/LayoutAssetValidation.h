#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <stdexcept>

namespace LayoutAssetValidation
{
    /// <summary>参照解決前後の差分検証が全体検証と一致し、不正な解決候補を拒否することを確認します。</summary>
    inline void Run(const std::filesystem::path& root)
    {
        using namespace SceneRuntime;
        const auto require = [](bool value, const char* message)
        {
            if (!value)
                throw std::runtime_error(message);
        };
        const Engine::AssetDatabase index(root, true);
        const std::filesystem::path target = "Assets/Models/Title/Commercial/detail-awning.obj";
        const std::string oldPath = "Assets/Models/Title/Commercial/previous-awning.obj";
        const auto id = index.ReferenceId(target);
        require(!id.empty(), "asset resolution validation fixture has a stable asset ID");
        SceneLayout before;
        ScenePlacement object;
        object.id = "asset-resolution";
        object.SetModel(oldPath);
        before.objects.push_back(object);
        before.assetReferences[oldPath] = id;
        before.Validate();
        const auto original = before.Serialize();
        for (const bool editing : {false, true})
        {
            auto resolved = before;
            if (editing)
                require(ResolveSceneAssetReferencesForEditing(resolved, index).empty(),
                        "editing asset references resolve without diagnostics");
            else
                resolved.ResolveAssets(index);
            resolved.ValidateChanges(before);
            resolved.Validate();
            require(resolved.objects.front().Model() == target &&
                        resolved.assetReferences.contains(target.generic_string()) && before.Serialize() == original,
                    "reference resolution updates paths and metadata while preserving the validated baseline");
            const auto reject = [&](SceneLayout invalid)
            {
                bool deltaRejected = false, fullRejected = false;
                try
                {
                    invalid.ValidateChanges(before);
                }
                catch (const std::exception&)
                {
                    deltaRejected = true;
                }
                try
                {
                    invalid.Validate();
                }
                catch (const std::exception&)
                {
                    fullRejected = true;
                }
                require(deltaRejected && fullRejected && before.Serialize() == original,
                        "invalid resolved candidate is rejected by both validators without changing the baseline");
            };
            auto invalid = resolved;
            invalid.objects.front().SetModel("../outside.obj");
            reject(std::move(invalid));
            invalid = resolved;
            invalid.assetReferences[target.generic_string()] = "invalid/id";
            reject(std::move(invalid));
            invalid = resolved;
            invalid.settings.skyShader = "../outside.hlsl";
            reject(std::move(invalid));
        }
    }
}
