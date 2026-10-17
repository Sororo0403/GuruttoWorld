#pragma once
#include <Engine/Core/Json.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace Engine
{
    class ContentFiles final
    {
    public:
        /// <summary>仮想アセットパスからプロジェクトのルートを求めます。</summary>
        static std::filesystem::path Root(std::filesystem::path path)
        {
            path = std::filesystem::absolute(path).lexically_normal().parent_path();
            while (!path.empty() && path != path.root_path())
            {
                if (path.filename() == "Assets" || path.filename() == "Shaders")
                    return path.parent_path();
                path = path.parent_path();
            }
            return {};
        }
        /// <summary>ゲームのエンジン参照から共有素材の正本を求めます。配布物は同梱ファイルだけを使います。</summary>
        static std::filesystem::path SharedRoot(const std::filesystem::path& root)
        {
            std::ifstream input(root / "WP1.project.json");
            if (!input)
                return {};
            const auto descriptor = Json::parse(input);
            const auto value = descriptor.at("engineRoot").get<std::string>();
            const auto engine = std::filesystem::path(std::u8string(value.begin(), value.end()));
            if (engine.empty())
                throw std::runtime_error("Empty engine content reference");
            return std::filesystem::weakly_canonical((engine.is_absolute() ? engine : root / engine) / "Content");
        }
        /// <summary>共有素材を先、ゲーム素材を後に列挙し、ゲーム側の上書きを優先します。</summary>
        static std::vector<std::filesystem::path> Layers(const std::filesystem::path& root)
        {
            std::vector<std::filesystem::path> layers;
            const auto shared = SharedRoot(root);
            if (!shared.empty() && shared != std::filesystem::weakly_canonical(root))
                layers.push_back(shared);
            layers.push_back(root);
            return layers;
        }
        /// <summary>同じ仮想パスに共有素材の正本があれば返します。</summary>
        static std::filesystem::path SharedFile(const std::filesystem::path& path)
        {
            const auto absolute = std::filesystem::absolute(path).lexically_normal();
            const auto root = Root(absolute);
            const auto relative = absolute.lexically_relative(root);
            const auto text = relative.generic_string();
            if (root.empty() || (!text.starts_with("Shaders/") && !text.starts_with("Assets/Fonts/")))
                return {};
            const auto shared = SharedRoot(root);
            if (!shared.empty() && std::filesystem::is_regular_file(shared / relative))
                return shared / relative;
            return {};
        }
        /// <summary>ゲームに実体がなければ、共有シェーダー・フォントとそのメタデータを参照します。</summary>
        static std::filesystem::path Resolve(const std::filesystem::path& path)
        {
            const auto absolute = std::filesystem::absolute(path).lexically_normal();
            if (std::filesystem::is_regular_file(absolute))
                return absolute;
            const auto shared = SharedFile(absolute);
            return shared.empty() ? absolute : shared;
        }
        /// <summary>正本を直接変更しないよう、共有素材への参照かを判定します。</summary>
        static bool IsShared(const std::filesystem::path& path)
        {
            return Resolve(path) != std::filesystem::absolute(path).lexically_normal();
        }
        /// <summary>同じ仮想パスを統合した素材一覧を返します。ゲーム側の実体が共有素材に優先します。</summary>
        static std::map<std::filesystem::path, std::filesystem::path> List(const std::filesystem::path& root)
        {
            std::map<std::filesystem::path, std::filesystem::path> files;
            for (const auto& layer : Layers(root))
                for (const auto* folder : {"Assets", "Shaders"})
                {
                    if (!std::filesystem::is_directory(layer / folder))
                        continue;
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(layer / folder))
                        if (entry.is_regular_file())
                            files[entry.path().lexically_relative(layer)] = entry.path();
                }
            return files;
        }
        /// <summary>共有素材を同じ ID のままゲーム側へ複製し、ゲーム固有の編集を可能にします。</summary>
        static void Override(const std::filesystem::path& root, const std::filesystem::path& relative)
        {
            const auto target = root / relative;
            const auto source = Resolve(target);
            if (!IsShared(target))
                throw std::runtime_error("Select shared content to create a game override");
            const auto destination =
                std::filesystem::weakly_canonical(target).lexically_relative(std::filesystem::weakly_canonical(root));
            if (destination.empty() || destination.is_absolute() || *destination.begin() == "..")
                throw std::runtime_error("Override destination leaves project root");
            auto metadata = source;
            metadata += ".meta";
            auto sidecar = target;
            sidecar += ".meta";
            if (std::filesystem::exists(sidecar))
                throw std::runtime_error("Override metadata already exists");
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(source, target);
            try
            {
                if (std::filesystem::is_regular_file(metadata))
                    std::filesystem::copy_file(metadata, sidecar);
            }
            catch (...)
            {
                std::filesystem::remove(target);
                throw;
            }
        }
    };
}
