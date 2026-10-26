#include "AssetChanges.h"

namespace Editor
{
    auto AssetChanges::Capture(const std::filesystem::path& root) -> Files
    {
        if (!std::filesystem::is_directory(root / "Assets"))
            throw std::runtime_error("変更監視対象のAssetsフォルダーがありません。");
        Files files;
        for (const auto& [relative, source] : Engine::ContentFiles::List(root))
            if (Watched(relative))
                files.emplace(source,
                              Stamp{std::filesystem::last_write_time(source), std::filesystem::file_size(source),
                                    ShaderHash(source), AssetRevision::Capture(source)});
        // 標準描画の変更も監視しますが、ゲームの素材一覧には登録しません。
        const auto shaders = Engine::ContentContext::Capture(root)->ShaderRoot();
        if (std::filesystem::is_directory(shaders))
            for (const auto& entry : std::filesystem::recursive_directory_iterator(shaders))
            {
                const auto relative = entry.path().lexically_relative(shaders);
                if (!entry.is_regular_file() || relative.generic_string().starts_with("Compiled/") ||
                    !Watched(relative))
                    continue;
                files.emplace(entry.path(), Stamp{std::filesystem::last_write_time(entry.path()), entry.file_size(),
                                                  ShaderHash(entry.path()), AssetRevision::Capture(entry.path())});
            }
        return files;
    }

    auto AssetChanges::Observe(Files files) -> void
    {
        if (!initialized_)
        {
            observed_ = std::move(files);
            initialized_ = true;
            return;
        }
        if (observed_ != files)
        {
            observed_ = std::move(files);
            pending_ = true;
            stable_ = false;
        }
        else if (pending_)
            stable_ = true;
    }

    auto AssetChanges::Poll(const std::filesystem::path& root, double seconds) -> void
    {
        // 完了した走査だけをエディターの状態へ反映します。フレーム内でディスク I/O の完了を待ちません。
        if (scan_.valid() && scan_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try
            {
                Observe(scan_.get());
                error_.clear();
            }
            catch (const std::exception& exception)
            {
                error_ = exception.what();
            }
        }
        if (!std::isfinite(seconds) || seconds <= 0)
            return;
        elapsed_ += std::min(seconds, 1.0);
        if (elapsed_ < 0.5 || scan_.valid())
            return;
        elapsed_ = 0;
        try
        {
            scan_ = std::async(std::launch::async,
                               [root]
                               {
                                   return Capture(root);
                               });
        }
        catch (const std::exception& exception)
        {
            error_ = exception.what();
        }
    }

    auto AssetChanges::TakeReady(bool editing) -> bool
    {
        if (!editing || !pending_ || !stable_ || !error_.empty())
            return false;
        pending_ = false;
        stable_ = false;
        return true;
    }

    auto AssetChanges::ShaderHash(const std::filesystem::path& path) -> std::uint64_t
    {
        const auto kind = ProjectCatalog::Kind(path);
        if (!kind || *kind != AssetKind::Shader)
            return 0;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Shaderの変更監視用データを読み取れません。");
        std::uint64_t hash = 14695981039346656037ULL;
        std::array<char, 4096> buffer{};
        while (input)
        {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            for (std::streamsize i = 0; i < input.gcount(); ++i)
                hash = (hash ^ static_cast<unsigned char>(buffer[static_cast<size_t>(i)])) * 1099511628211ULL;
        }
        if (!input.eof() || input.bad())
            throw std::runtime_error("Shaderの変更監視用データを読み取れません。");
        return hash;
    }

    auto AssetChanges::Extension(const std::filesystem::path& path) -> std::string
    {
        auto value = ProjectCatalog::Text(path.extension());
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });
        return value;
    }

    auto AssetChanges::Watched(const std::filesystem::path& path) -> bool
    {
        const auto kind = ProjectCatalog::Kind(path);
        return (kind && *kind != AssetKind::Scene) || Extension(path) == ".mtl" || Extension(path) == ".meta" ||
               Extension(path) == ".bin";
    }

    bool ValidateProjectShaders(const std::filesystem::path& root, std::string& error)
    {
        try
        {
            const auto validate = [&](const std::filesystem::path& shader)
            {
                Microsoft::WRL::ComPtr<ID3DBlob> vertex, pixel;
                if (Engine::CompileShader(shader, "VSMain", "vs_5_0", vertex) &&
                    Engine::CompileShader(shader, "PSMain", "ps_5_0", pixel))
                    return true;
                error = "シェーダーの再読み込みに失敗しました：" + shader.filename().string() +
                        "（コンソールを確認してください）";
                return false;
            };
            for (const auto& [relative, source] : Engine::ContentFiles::List(root))
            {
                if (!relative.generic_string().starts_with("Shaders/") ||
                    relative.generic_string().starts_with("Shaders/Compiled/") ||
                    !AssetChanges::IsShaderSource(relative))
                    continue;
                if (!validate(source))
                    return false;
            }
            const auto shaders = Engine::ContentContext::Capture(root)->ShaderRoot();
            if (std::filesystem::is_directory(shaders))
                for (const auto& entry : std::filesystem::recursive_directory_iterator(shaders))
                {
                    const auto relative = entry.path().lexically_relative(shaders);
                    if (!entry.is_regular_file() || relative.generic_string().starts_with("Compiled/") ||
                        !AssetChanges::IsShaderSource(relative))
                        continue;
                    if (!validate(entry.path()))
                        return false;
                }
            error.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
}
