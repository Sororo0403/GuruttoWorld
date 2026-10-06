#pragma once
#include "ProjectCatalog.h"
#include <fstream>
#include <cstdint>
#include <stdexcept>

namespace Editor
{
    struct AssetInfo
    {
        std::uintmax_t bytes=0;
        std::string text, error;
        bool truncated=false;
        static AssetInfo Read(const std::filesystem::path& root, const ProjectAsset& asset)
        {
            AssetInfo result;
            try
            {
                if (asset.path.is_absolute() || std::any_of(asset.path.begin(),asset.path.end(),
                    [](const auto& part) { return part==".."; })) throw std::runtime_error("Invalid project asset path");
                const auto path=root/asset.path;
                result.bytes=std::filesystem::file_size(path);
                if (asset.kind==AssetKind::Model || asset.kind==AssetKind::Scene || asset.kind==AssetKind::Shader)
                {
                    std::ifstream stream(path,std::ios::binary);
                    if (!stream) throw std::runtime_error("Asset could not be opened");
                    result.text.resize(8192);
                    stream.read(result.text.data(),static_cast<std::streamsize>(result.text.size()));
                    if (stream.bad()) throw std::runtime_error("Asset could not be read");
                    result.text.resize(static_cast<size_t>(stream.gcount()));
                    result.truncated=result.bytes>result.text.size();
                    if (result.text.find('\0')!=std::string::npos) throw std::runtime_error("Source preview contains binary data");
                }
            }
            catch (const std::exception& exception) { result.text.clear(); result.error=exception.what(); }
            return result;
        }
    };
}
