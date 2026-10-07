#pragma once
#include <Engine/Graphics/Materials/Material.h>
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
        static bool ValidPath(const std::filesystem::path& path)
        {
            const auto utf8=path.generic_u8string(); const std::string value(utf8.begin(),utf8.end());
            return !path.is_absolute() && !path.has_root_name() && value.starts_with("Assets/Materials/") && path.extension()==".mat" &&
                std::none_of(path.begin(),path.end(),[](const auto& part) { return part==".."; });
        }
        void Validate() const
        {
            for (const float channel : values.color) if (!std::isfinite(channel) || channel<0 || channel>1) throw std::runtime_error("Invalid material color");
            if (!std::isfinite(values.roughness) || values.roughness<0.04f || values.roughness>1 ||
                !std::isfinite(values.metallic) || values.metallic<0 || values.metallic>1) throw std::runtime_error("Invalid material surface");
            const auto text=texture.generic_u8string(); const std::string path(text.begin(),text.end());
            if (!texture.empty() && (texture.is_absolute() || texture.has_root_name() || !path.starts_with("Assets/Textures/") ||
                std::any_of(texture.begin(),texture.end(),[](const auto& part) { return part==".."; }))) throw std::runtime_error("Invalid material texture path");
            for (const auto value : {values.uv.scale[0],values.uv.scale[1],values.uv.rotation,values.uv.translation[0],values.uv.translation[1]})
                if (!std::isfinite(value) || std::abs(value)>100000) throw std::runtime_error("Invalid material UV transform");
        }
        static MaterialAsset Load(const std::filesystem::path& root,const std::filesystem::path& path)
        {
            if (!ValidPath(path)) throw std::runtime_error("Invalid material path");
            std::ifstream stream(root/path); if (!stream) throw std::runtime_error("Cannot open material");
            auto json=Engine::Json::parse(stream); Engine::AssetDatabase(root).References(json); MaterialAsset asset;
            const auto& color=Engine::JsonArray(json.at("color")); if (color.size()!=4) throw std::runtime_error("Material color needs four channels");
            for (size_t index=0;index<4;++index) asset.values.color[index]=static_cast<float>(Engine::JsonNumber(color[index]));
            asset.values.roughness=static_cast<float>(Engine::JsonNumber(json.at("roughness")));
            asset.values.metallic=static_cast<float>(Engine::JsonNumber(json.at("metallic")));
            asset.values.transparent=json.at("transparent").get<bool>();
            const auto texture=json.at("texture").get<std::string>(); asset.texture=std::filesystem::path(std::u8string(texture.begin(),texture.end()));
            if (json.contains("uv"))
            {
                const auto& uv=json.at("uv");
                for (const char* key : {"scale","translation"})
                {
                    const auto& array=Engine::JsonArray(uv.at(key)); if (array.size()!=2) throw std::runtime_error("Material UV needs two components");
                    auto& target=std::string_view(key)=="scale" ? asset.values.uv.scale : asset.values.uv.translation;
                    for (size_t index=0;index<2;++index) target[index]=static_cast<float>(Engine::JsonNumber(array[index]));
                }
                asset.values.uv.rotation=static_cast<float>(Engine::JsonNumber(uv.at("rotation")));
            }
            asset.Validate(); return asset;
        }
        void Save(const std::filesystem::path& root,const std::filesystem::path& path) const
        {
            Validate(); if (!texture.empty() && !std::filesystem::is_regular_file(root/texture)) throw std::runtime_error("Material texture does not exist");
            if (!ValidPath(path)) throw std::runtime_error("Invalid material path");
            std::filesystem::create_directories((root/path).parent_path());
            const auto text=texture.generic_u8string();
            Engine::Json json={{"color",values.color},{"roughness",values.roughness},{"metallic",values.metallic},
                {"transparent",values.transparent},{"texture",std::string(text.begin(),text.end())},
                {"uv",{{"scale",values.uv.scale},{"rotation",values.uv.rotation},{"translation",values.uv.translation}}}};
            Engine::AssetDatabase(root).References(json);
            auto temporary=root/path; temporary+=".tmp";
            std::ofstream output(temporary,std::ios::binary|std::ios::trunc); output<<json.dump(2)<<'\n'; output.close();
            if (!output || !MoveFileExW(temporary.c_str(),(root/path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            { std::error_code error; std::filesystem::remove(temporary,error); throw std::runtime_error("Cannot save material"); }
        }
        std::shared_ptr<const Engine::Material> Prepare(ID3D12Device* device,ID3D12CommandQueue* queue,const std::filesystem::path& root) const
        {
            Validate(); auto result=std::make_shared<Engine::Material>(values);
            if (!texture.empty())
            {
                auto image=std::make_shared<Engine::Texture2D>();
                if (!image->Initialize(device,queue,root/texture)) throw std::runtime_error("Cannot load material texture");
                result->texture=std::move(image);
            }
            return result;
        }
    };
}
