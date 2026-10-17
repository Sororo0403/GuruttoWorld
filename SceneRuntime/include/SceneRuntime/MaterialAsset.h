#pragma once
#include <Engine/Core/Validation.h>
#include <array>
#include <iterator>
#include <Engine/Core/Profiler.h>
#include <Engine/Graphics/Materials/Material.h>
#include <Engine/Graphics/Renderers/MeshResources.h>
#include <Engine/Core/Json.h>
#include <Engine/Assets/AssetDatabase.h>
#include <fstream>
#include <Engine/Platform/Window.h>
#include <filesystem>
#include <cmath>
#include <algorithm>

namespace SceneRuntime
{
    struct MaterialAsset
    {
        Engine::Material values;
        std::filesystem::path texture;
        std::filesystem::path normalTexture;
        std::filesystem::path environmentTexture, lightmap;
        std::filesystem::path shader;
        static bool ValidPath(const std::filesystem::path& path)
        {
            const auto utf8 = path.generic_u8string();
            const std::string value(utf8.begin(), utf8.end());
            return !path.is_absolute() && !path.has_root_name() && value.starts_with("Assets/Materials/") &&
                   path.extension() == ".mat" &&
                   std::none_of(path.begin(), path.end(),
                                [](const auto& part)
                                {
                                    return part == "..";
                                });
        }
        void ValidateTextures() const
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
        void Validate() const
        {
            if (!shader.empty() &&
                (shader.is_absolute() || shader.has_root_name() ||
                 !shader.generic_u8string().starts_with(u8"Shaders/") || shader.extension() != ".hlsl" ||
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
            if (!Engine::InRange(values.effectAmount, 0, 1))
                throw std::runtime_error("Invalid material effect amount");
            if (!Engine::InRange(values.effectVariant, 0, 2))
                throw std::runtime_error("Invalid material effect variant");
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
        static MaterialAsset Load(const std::filesystem::path& root, const std::filesystem::path& path,
                                  const Engine::AssetDatabase* assetIndex = nullptr)
        {
            if (!ValidPath(path))
                throw std::runtime_error("Invalid material path");
            std::ifstream stream(root / path);
            if (!stream)
                throw std::runtime_error("Cannot open material");
            auto json = Engine::Json::parse(stream);
            if (assetIndex)
                assetIndex->References(json);
            else
            {
                Engine::CpuScope scope("Material asset database scan");
                Engine::AssetDatabase(root).References(json);
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
            asset.values.effectAmount = json.value("effectAmount", 0.0f);
            asset.values.effectVariant = json.value("effectVariant", 0.0f);
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
                    auto& target =
                        std::string_view(key) == "scale" ? asset.values.uv.scale : asset.values.uv.translation;
                    for (size_t index = 0; index < 2; ++index)
                        target[index] = static_cast<float>(Engine::JsonNumber(array[index]));
                }
                asset.values.uv.rotation = static_cast<float>(Engine::JsonNumber(uv.at("rotation")));
            }
            asset.Validate();
            return asset;
        }
        void Save(const std::filesystem::path& root, const std::filesystem::path& path) const
        {
            Validate();
            if (!shader.empty() && !std::filesystem::is_regular_file(root / shader))
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
            std::filesystem::create_directories((root / path).parent_path());
            const auto text = texture.generic_u8string();
            const auto normal = normalTexture.generic_u8string();
            Engine::Json json = {{"color", values.color},
                                 {"roughness", values.roughness},
                                 {"metallic", values.metallic},
                                 {"effectAmount", values.effectAmount},
                                 {"effectVariant", values.effectVariant},
                                 {"transparent", values.transparent},
                                 {"texture", std::string(text.begin(), text.end())},
                                 {"physicallyBased", values.physicallyBased},
                                 {"normalFlipY", values.normalFlipY},
                                 {"normalTexture", std::string(normal.begin(), normal.end())},
                                 {"environmentTexture", environmentTexture},
                                 {"environmentIntensity", values.environmentIntensity},
                                 {"lightmap", lightmap},
                                 {"uv",
                                  {{"scale", values.uv.scale},
                                   {"rotation", values.uv.rotation},
                                   {"translation", values.uv.translation}}}};
            if (!shader.empty())
            {
                const auto bytes = shader.generic_u8string();
                json["shader"] = std::string(bytes.begin(), bytes.end());
            }
            Engine::AssetDatabase(root).References(json);
            auto temporary = root / path;
            temporary += ".tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << json.dump(2) << '\n';
            output.close();
            if (!output || !MoveFileExW(temporary.c_str(), (root / path).c_str(),
                                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                std::error_code error;
                std::filesystem::remove(temporary, error);
                throw std::runtime_error("Cannot save material");
            }
        }
        std::shared_ptr<const Engine::Material> Prepare(ID3D12Device* device, ID3D12CommandQueue* queue,
                                                        const std::filesystem::path& root) const
        {
            Validate();
            auto result = std::make_shared<Engine::Material>(values);
            if (!shader.empty())
            {
                auto resources = std::make_shared<Engine::MeshResources>();
                if (!resources->Initialize(device, root / shader, queue))
                    throw std::runtime_error("Cannot initialize material shader (Mesh shader interface required)");
                result->shaderResources = std::move(resources);
            }
            if (!texture.empty())
            {
                auto image = std::make_shared<Engine::Texture2D>();
                if (!image->Initialize(device, queue, root / texture))
                    throw std::runtime_error("Cannot load material texture");
                result->texture = std::move(image);
            }
            if (!normalTexture.empty())
            {
                auto image = std::make_shared<Engine::Texture2D>();
                if (!image->Initialize(device, queue, root / normalTexture))
                    throw std::runtime_error("Cannot load material normal texture");
                result->normalTexture = std::move(image);
            }
            for (const auto& entry :
                 {std::pair{environmentTexture, &result->environmentTexture}, std::pair{lightmap, &result->lightmap}})
                if (!entry.first.empty())
                {
                    auto image = std::make_shared<Engine::Texture2D>();
                    if (!image->Initialize(device, queue, root / entry.first))
                        throw std::runtime_error("Cannot load material lighting texture");
                    *entry.second = std::move(image);
                }
            return result;
        }
    };
}
