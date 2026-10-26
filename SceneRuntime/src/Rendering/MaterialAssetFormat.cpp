#include <SceneRuntime/Rendering/MaterialAssetFormat.h>

namespace SceneRuntime
{
    auto MaterialAssetFormat::ReadShaderParameters(const Engine::Json& source) -> std::array<float, 4>
    {
        std::array<float, 4> result{};
        if (!source.contains("shaderParameters"))
            return result;
        const auto& values = Engine::JsonArray(source.at("shaderParameters"));
        if (values.size() != result.size())
            throw std::runtime_error("Shader parameters need four components");
        for (size_t index = 0; index < result.size(); ++index)
        {
            result[index] = static_cast<float>(Engine::JsonNumber(values[index]));
            if (!std::isfinite(result[index]))
                throw std::runtime_error("Shader parameters must be finite floats");
        }
        return result;
    }

    auto MaterialAssetFormat::ValidPath(const std::filesystem::path& path) -> bool
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
}
