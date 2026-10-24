#include "TextureImportValidation.h"
#include <iostream>

namespace
{
    // 実素材に近い100MiB超の作業集合で、基底画素の二重保持や早すぎる退避を検出します。
    void WorkingSet(Engine::DirectX12Renderer& renderer)
    {
        constexpr unsigned int TextureCount = 28;
        Engine::TextureImportSettings settings;
        settings.mipmaps = true;
        for (unsigned int pass = 0; pass < 2; ++pass)
        {
            static_cast<void>(Engine::CpuProfiler::Take());
            for (unsigned int index = 0; index < TextureCount; ++index)
            {
                Engine::Texture2D texture;
                EnvironmentValidation::Require(
                    texture.InitializePixels(
                        renderer.GetDevice(), renderer.GetCommandQueue(), 1024, 1024,
                        std::vector<unsigned char>(1024 * 1024 * 4, static_cast<unsigned char>(121 + index)), settings),
                    "texture working set uploads");
            }
            if (pass == 1)
            {
                unsigned int hits = 0;
                for (const auto& sample : Engine::CpuProfiler::Take())
                    if (sample.name == "Texture mip cache hit")
                        hits += sample.calls;
                EnvironmentValidation::Require(hits == TextureCount, "realistic texture working set remains reusable");
            }
        }
    }
}

int main()
{
    try
    {
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        EnvironmentValidation::Require(window.Create(L"Texture conversion validation", 64, 64) &&
                                           renderer.Initialize(window.GetHandle()),
                                       "texture validation renderer");
        TextureImportValidation::Run(renderer);
        WorkingSet(renderer);
        EnvironmentValidation::Require(renderer.WaitForIdle(), "texture verification GPU completes");
        std::cout << "PASS: texture conversion reuse, all import settings, GPU rendering and working set\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
