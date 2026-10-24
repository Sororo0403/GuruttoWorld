#include "PipelineReuseValidation.h"
#include <iostream>

int main()
{
    try
    {
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        if (!window.Create(L"Pipeline reuse validation", 64, 64) || !renderer.Initialize(window.GetHandle()))
            throw std::runtime_error("pipeline renderer initialization failed");
        const auto root = std::filesystem::absolute("generated/tests/pipeline-reuse") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        PipelineReuseValidation::Run(renderer, root);
        std::cout << "PASS: pipeline reuse, shader invalidation and device cleanup\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
