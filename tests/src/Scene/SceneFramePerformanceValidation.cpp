#include <SceneRuntime/Scene/SceneWorld.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Platform/Window.h>
#include <Engine/Core/Json.h>
#include <Engine/Core/Profiler.h>
#include "../Support/AllocationProbe.h"
#include <chrono>
#include <fstream>
#include <iostream>

namespace
{
    using namespace SceneRuntime;

    std::uint64_t Hash(const std::string& value)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto character : value)
        {
            hash ^= static_cast<unsigned char>(character);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    SceneLayout Fixture(size_t count, bool scripts, bool physics)
    {
        SceneLayout layout;
        layout.objects.reserve(count);
        for (size_t index = 0; index < count; ++index)
        {
            ScenePlacement object;
            object.id = "object-" + std::to_string(index);
            object.name = "Runtime object " + std::to_string(index);
            object.position = {static_cast<float>(index % 100) * 2, physics ? 10.0f : 0.0f,
                               static_cast<float>(index / 100) * 2};
            object.material = MaterialComponent{};
            if (scripts)
                object.scripts.push_back(ScriptComponent{});
            if (physics)
            {
                object.boxCollider = BoxColliderComponent{};
                object.rigidBody = RigidBodyComponent{};
            }
            layout.objects.push_back(std::move(object));
        }
        layout.Validate();
        return layout;
    }

    Engine::Json Measure(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root, size_t count,
                         bool scripts, bool physics)
    {
        const auto layout = Fixture(count, scripts, physics);
        const auto inputHash = Hash(layout.Serialize());
        SceneWorld world;
        std::string error;
        if (!world.Initialize(renderer, root, layout, root / "Engine/Shaders/Mesh.hlsl", &error) ||
            !world.StartScripts(error))
            throw std::runtime_error(error);
        world.TakeChanges();
        std::vector<double> samples, allocations, bytes;
        std::map<std::string, double> phaseMilliseconds;
        for (size_t frame = 0; frame < 72; ++frame)
        {
            Engine::CpuProfiler::Take();
            double elapsed;
            TestSupport::AllocationCounts memory;
            bool success;
            {
                TestSupport::AllocationProbe probe;
                const auto start = std::chrono::steady_clock::now();
                success = world.UpdateFrame(1.0 / 60.0, 0, 0, false, physics);
                elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                memory = probe.Counts();
            }
            if (!success)
                throw std::runtime_error("Scene frame update failed");
            world.TakeChanges();
            const auto phases = Engine::CpuProfiler::Take();
            if (frame >= 8)
            {
                samples.push_back(elapsed);
                allocations.push_back(static_cast<double>(memory.count));
                bytes.push_back(static_cast<double>(memory.bytes));
                for (const auto& phase : phases)
                    phaseMilliseconds[phase.name] += phase.milliseconds / 64;
            }
        }
        const auto percentile = [](std::vector<double> values, double fraction)
        {
            std::sort(values.begin(), values.end());
            return values[static_cast<size_t>((values.size() - 1) * fraction)];
        };
        const auto resultJson = world.Layout().Serialize();
        if (!scripts && !physics && Hash(resultJson) != inputHash)
            throw std::runtime_error("Static fixture changed");
        Engine::Json result{{"objects", count},
                            {"scripts", scripts},
                            {"physics", physics},
                            {"inputHash", std::to_string(inputHash)},
                            {"resultHash", std::to_string(Hash(resultJson))},
                            {"warmupFrames", 8},
                            {"measuredFrames", samples.size()},
                            {"medianMs", percentile(samples, .5)},
                            {"p95Ms", percentile(samples, .95)},
                            {"p99Ms", percentile(samples, .99)},
                            {"medianAllocations", percentile(allocations, .5)},
                            {"medianRequestedBytes", percentile(bytes, .5)},
                            {"phaseInclusiveMeanMs", phaseMilliseconds}};
        std::cout << result.dump() << '\n';
        return result;
    }
}

int main(int argc, char** argv)
{
    try
    {
        const auto root = std::filesystem::absolute("generated/tests/frame-performance") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Engine");
        std::filesystem::copy("Engine/Shaders", root / "Engine/Shaders", std::filesystem::copy_options::recursive);
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        if (!window.Create(L"Scene CPU frame measurement", 64, 64) || !renderer.Initialize(window.GetHandle()))
            throw std::runtime_error("Cannot initialize measurement renderer");
        const auto adapter = renderer.GetDevice()->GetAdapterLuid();
        Engine::Json result{{"path", "RuntimeSession through SceneWorld sparse projection"},
                            {"stepSeconds", 1.0 / 60.0},
                            {"scope", "CPU update transaction; no draw/Present; main-thread C++ allocations only"},
                            {"adapterLuidHigh", adapter.HighPart},
                            {"adapterLuidLow", adapter.LowPart},
                            {"scenarios", Engine::Json::array()}};
        for (const auto count : {size_t{0}, size_t{1000}, size_t{5000}, size_t{10000}})
            result["scenarios"].push_back(Measure(renderer, root, count, false, false));
        result["scenarios"].push_back(Measure(renderer, root, 1000, true, false));
        for (const auto count : {size_t{100}, size_t{1000}})
            result["scenarios"].push_back(Measure(renderer, root, count, false, true));
        if (!renderer.WaitForIdle())
            throw std::runtime_error("Measurement GPU completion failed");
        if (argc > 1)
        {
            const auto path = std::filesystem::path(argv[1]);
            std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path);
            output << result.dump(2) << '\n';
            if (!output)
                throw std::runtime_error("Cannot save frame measurement");
        }
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
