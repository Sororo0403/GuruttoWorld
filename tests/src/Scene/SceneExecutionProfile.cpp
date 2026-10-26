#include <SceneRuntime/Scene/SceneWorld.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Profiler.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>

namespace
{
    /// <summary>描画を含まないWorld更新を計測し、内側の更新時間と外側の取引費用を記録します。</summary>
    void Measure(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root, size_t count, bool physics,
                 bool active = false)
    {
        SceneRuntime::SceneLayout layout;
        layout.objects.reserve(count);
        for (size_t index = 0; index < count; ++index)
        {
            SceneRuntime::ScenePlacement object;
            object.id = "profile-" + std::to_string(index);
            object.position = {static_cast<float>(index % 25) * 3.0f, 0, static_cast<float>(index / 25) * 3.0f};
            if (active)
            {
                object.rotator.emplace();
                if (index % 100 != 0)
                    object.parentId = "profile-" + std::to_string(index - index % 100);
                else
                    object.scripts.emplace_back().behaviour = "ProfileMovement";
            }
            if (physics)
            {
                object.boxCollider.emplace();
                object.rigidBody.emplace().gravityScale = 0;
            }
            layout.objects.push_back(std::move(object));
        }
        SceneRuntime::SceneWorld world;
        std::string error;
        if (!world.Initialize(renderer, root, std::move(layout), root / "Engine/Shaders/Mesh.hlsl", &error))
            throw std::runtime_error(error);
        if (!world.StartScripts(error))
            throw std::runtime_error(error);
        constexpr size_t Warmup = 3;
        constexpr size_t Frames = 20;
        for (size_t frame = 0; frame < Warmup; ++frame)
            if (!world.UpdateFrame(1.0 / 60, 0, 0, false, physics))
                throw std::runtime_error("Profile warmup failed");
        Engine::CpuProfiler::Take();
        const auto start = std::chrono::steady_clock::now();
        for (size_t frame = 0; frame < Frames; ++frame)
            if (!world.UpdateFrame(1.0 / 60, 0, 0, false, physics))
                throw std::runtime_error("Profile frame failed");
        const double total =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        double components = 0, movement = 0;
        for (const auto& sample : Engine::CpuProfiler::Take())
        {
            if (sample.name == "Components update")
                components = sample.milliseconds;
            if (sample.name == "Physics and player movement")
                movement = sample.milliseconds;
        }
        // 残差は候補の複製・保存点・確定・計測を含みます。旧実装とは区間境界が異なり、合計時間だけを比較します。
        std::cout << count << ',' << physics << ',' << Frames << ',' << total / Frames << ',' << components / Frames
                  << ',' << movement / Frames << ',' << std::max(0.0, total - components - movement) / Frames << ','
                  << (active    ? "hierarchy-rotators-scripts"
                      : physics ? "rigid-bodies"
                                : "placements")
                  << '\n';
        if (!renderer.WaitForIdle())
            throw std::runtime_error("Profile GPU completion failed");
    }
}

int main()
{
    try
    {
        const auto root = std::filesystem::absolute("generated/tests/scene-execution-profile") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root);
        std::filesystem::create_directories(root / "Engine");
        std::filesystem::copy("Engine/Shaders", root / "Engine/Shaders", std::filesystem::copy_options::recursive);
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        if (!window.Create(L"Scene execution profile", 64, 64) || !renderer.Initialize(window.GetHandle()))
            throw std::runtime_error("Profile renderer initialization failed");
        SceneRuntime::ScriptDefinition definition;
        definition.update = [](SceneRuntime::ScriptContext& context)
        {
            context.object.position[0] += .01f;
        };
        if (!SceneRuntime::ScriptRegistry::Register("ProfileMovement", definition))
            throw std::runtime_error("Profile script registration failed");
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "objects,physics,frames,frame_ms,components_ms,movement_ms,transaction_and_other_ms,workload\n";
        for (const size_t count : {size_t{0}, size_t{1000}, size_t{5000}})
            Measure(renderer, root, count, false);
        Measure(renderer, root, 100, true);
        Measure(renderer, root, 1000, false, true);
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
