#include <SceneRuntime/Rendering/MaterialAsset.h>

namespace SceneRuntime
{
    auto MaterialAsset::ValidateTextures() const -> void
    {
        for (const auto& image : {texture, normalTexture, environmentTexture, lightmap})
        {
            const auto text = image.generic_u8string();
            const std::string path(text.begin(), text.end());
            if (!image.empty() &&
                (image.is_absolute() || image.has_root_name() || !path.starts_with("Assets/Textures/") ||
                 std::any_of(image.begin(), image.end(),
                             [](const auto& part)
                             {
                                 return part == "..";
                             })))
                throw std::runtime_error("Invalid material texture path");
        }
    }

    auto MaterialAsset::Validate() const -> void
    {
        if (!shader.empty() && (shader.is_absolute() || shader.has_root_name() ||
                                (!shader.generic_u8string().starts_with(u8"Shaders/") &&
                                 !shader.generic_u8string().starts_with(u8"Engine/Shaders/")) ||
                                shader.extension() != ".hlsl" ||
                                std::any_of(shader.begin(), shader.end(),
                                            [](const auto& part)
                                            {
                                                return part == "..";
                                            })))
            throw std::runtime_error("Invalid material shader path");
        if (std::any_of(std::begin(values.color), std::end(values.color),
                        [](const float channel)
                        {
                            return !Engine::InRange(channel, 0, 1);
                        }))
            throw std::runtime_error("Invalid material color");
        if (std::any_of(std::begin(values.shaderParameters), std::end(values.shaderParameters),
                        [](float value)
                        {
                            return !Engine::InRange(value, -100000, 100000);
                        }))
            throw std::runtime_error("Invalid material shader parameter");
        if (!Engine::InRange(values.roughness, 0.04f, 1) || !Engine::InRange(values.metallic, 0, 1))
            throw std::runtime_error("Invalid material surface");
        if (!Engine::InRange(values.environmentIntensity, 0, 100))
            throw std::runtime_error("Invalid environment intensity");
        ValidateTextures();
        const auto validationValues1 = std::array{values.uv.scale[0], values.uv.scale[1], values.uv.rotation,
                                                  values.uv.translation[0], values.uv.translation[1]};
        if (std::any_of(validationValues1.begin(), validationValues1.end(),
                        [](const auto value)
                        {
                            return !Engine::InRange(value, -100000, 100000);
                        }))
            throw std::runtime_error("Invalid material UV transform");
    }

    auto MaterialAsset::Load(const std::filesystem::path& root, const std::filesystem::path& path,
                             const Engine::AssetDatabase* assetIndex) -> MaterialAsset
    {
        if (!ValidPath(path))
            throw std::runtime_error("Invalid material path");
        const auto source = Engine::FileContents::Read(root / path);
        if (!source)
            throw std::runtime_error("Cannot open material");
        auto json = Engine::Json::parse(*source);
        if (assetIndex)
            References(json, *assetIndex);
        else
        {
            Engine::CpuScope scope("Material asset database scan");
            References(json, Engine::AssetDatabase(root));
        }
        MaterialAsset asset;
        const auto shader = json.value("shader", std::string{});
        asset.shader = std::filesystem::path(std::u8string(shader.begin(), shader.end()));
        const auto& color = Engine::JsonArray(json.at("color"));
        if (color.size() != 4)
            throw std::runtime_error("Material color needs four channels");
        for (size_t index = 0; index < 4; ++index)
            asset.values.color[index] = static_cast<float>(Engine::JsonNumber(color[index]));
        asset.values.roughness = static_cast<float>(Engine::JsonNumber(json.at("roughness")));
        asset.values.metallic = static_cast<float>(Engine::JsonNumber(json.at("metallic")));
        asset.values.shaderParameters = ReadShaderParameters(json);
        asset.values.transparent = json.at("transparent").get<bool>();
        const auto texture = json.at("texture").get<std::string>();
        asset.texture = std::filesystem::path(std::u8string(texture.begin(), texture.end()));
        asset.values.physicallyBased = json.value("physicallyBased", false);
        asset.values.normalFlipY = json.value("normalFlipY", false);
        const auto normal = json.value("normalTexture", std::string{});
        asset.normalTexture = std::filesystem::path(std::u8string(normal.begin(), normal.end()));
        const auto environment = json.value("environmentTexture", std::string{}),
                   baked = json.value("lightmap", std::string{});
        asset.environmentTexture = std::filesystem::path(std::u8string(environment.begin(), environment.end()));
        asset.lightmap = std::filesystem::path(std::u8string(baked.begin(), baked.end()));
        asset.values.environmentIntensity = json.value("environmentIntensity", 1.0f);
        if (json.contains("uv"))
        {
            const auto& uv = json.at("uv");
            for (const char* key : {"scale", "translation"})
            {
                const auto& array = Engine::JsonArray(uv.at(key));
                if (array.size() != 2)
                    throw std::runtime_error("Material UV needs two components");
                auto& target = std::string_view(key) == "scale" ? asset.values.uv.scale : asset.values.uv.translation;
                for (size_t index = 0; index < 2; ++index)
                    target[index] = static_cast<float>(Engine::JsonNumber(array[index]));
            }
            asset.values.uv.rotation = static_cast<float>(Engine::JsonNumber(uv.at("rotation")));
        }
        asset.Validate();
        return asset;
    }

    auto MaterialAsset::SerializeForSave(const std::filesystem::path& root, const std::filesystem::path& path) const
        -> std::string
    {
        Validate();
        if (!shader.empty() && !std::filesystem::is_regular_file(Engine::ContentFiles::Resolve(root / shader)))
            throw std::runtime_error("Material shader does not exist");
        const auto validationValues2 = std::array{texture, normalTexture, environmentTexture, lightmap};
        if (std::any_of(validationValues2.begin(), validationValues2.end(),
                        [&root](const auto& image)
                        {
                            return !image.empty() && !std::filesystem::is_regular_file(root / image);
                        }))
            throw std::runtime_error("Material texture does not exist");
        if (!ValidPath(path))
            throw std::runtime_error("Invalid material path");
        const auto text = texture.generic_u8string();
        const auto normal = normalTexture.generic_u8string();
        Engine::Json json = {
            {"color", values.color},
            {"roughness", values.roughness},
            {"metallic", values.metallic},
            {"shaderParameters", values.shaderParameters},
            {"transparent", values.transparent},
            {"texture", std::string(text.begin(), text.end())},
            {"physicallyBased", values.physicallyBased},
            {"normalFlipY", values.normalFlipY},
            {"normalTexture", std::string(normal.begin(), normal.end())},
            {"environmentTexture", environmentTexture},
            {"environmentIntensity", values.environmentIntensity},
            {"lightmap", lightmap},
            {"uv",
             {{"scale", values.uv.scale}, {"rotation", values.uv.rotation}, {"translation", values.uv.translation}}}};
        if (!shader.empty())
        {
            const auto bytes = shader.generic_u8string();
            json["shader"] = std::string(bytes.begin(), bytes.end());
        }
        References(json, Engine::AssetDatabase(root));
        return json.dump(2) + "\n";
    }

    auto MaterialAsset::Save(const std::filesystem::path& root, const std::filesystem::path& path) const -> void
    {
        Engine::FileReplacement::Save(root / path,
                                      [&]
                                      {
                                          return SerializeForSave(root, path);
                                      });
    }

    auto MaterialAsset::Prepare(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& root,
                                std::shared_ptr<const Engine::ContentContext> context) const
        -> std::shared_ptr<const Engine::Material>
    {
        Validate();
        if (context)
        {
            context->RequireRoot(root);
            context->RequireConfiguration();
        }
        auto result = std::make_shared<Engine::Material>(values);
        if (!shader.empty())
        {
            auto resources = std::make_shared<Engine::MeshResources>();
            if (!resources->Initialize(
                    device, context ? context->Resolve(shader) : Engine::ContentFiles::Resolve(root / shader), queue))
                throw std::runtime_error("Cannot initialize material shader (Mesh shader interface required)");
            result->shaderResources = std::move(resources);
        }
        if (!texture.empty())
        {
            result->texture = MaterialTextureCache::Load(device, queue, root / texture);
        }
        if (!normalTexture.empty())
        {
            result->normalTexture = MaterialTextureCache::Load(device, queue, root / normalTexture);
        }
        for (const auto& entry :
             {std::pair{environmentTexture, &result->environmentTexture}, std::pair{lightmap, &result->lightmap}})
            if (!entry.first.empty())
            {
                *entry.second = MaterialTextureCache::Load(device, queue, root / entry.first);
            }
        return result;
    }

    auto MaterialAsset::References(Engine::Json& json, const Engine::AssetDatabase& index) -> void
    {
        const auto saved = json.value("assetReferences", Engine::AssetReferenceResolver::References{});
        Engine::AssetReferenceResolver references(index, saved);
        for (const auto* key : {"shader", "texture", "normalTexture", "environmentTexture", "lightmap"})
        {
            if (json.contains(key))
                references.Visit(json.at(key).get_ref<std::string&>());
        }
        json["assetReferences"] = references.Captured();
    }
}
