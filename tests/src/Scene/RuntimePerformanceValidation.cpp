#include <SceneRuntime/Scripts/RuntimeScripts.h>
#include "../Support/AllocationProbe.h"
#include <Engine/Core/Json.h>
#include <Engine/Core/Profiler.h>
#include <chrono>
#include <fstream>
#include <iostream>

#if defined(__d3d12_h__)
#error Runtime CPU measurement must not initialize or include the rendering API
#endif

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

    SceneLayout Fixture(size_t count, bool scripted)
    {
        SceneLayout layout;
        layout.objects.reserve(count);
        for (size_t index = 0; index < count; ++index)
        {
            ScenePlacement object;
            object.id = "object-" + std::to_string(index);
            object.name = "Static object " + std::to_string(index);
            object.position = {static_cast<float>(index % 100), 0, static_cast<float>(index / 100)};
            object.meshRenderer = MeshRendererComponent{};
            object.meshRenderer->enabled = false;
            object.meshRenderer->model = "Assets/Models/Static.glb";
            object.material = MaterialComponent{};
            if (scripted)
                object.scripts.push_back(ScriptComponent{});
            layout.objects.push_back(std::move(object));
        }
        layout.Validate();
        return layout;
    }

    Engine::Json Measure(size_t count, bool scripted)
    {
        auto layout = Fixture(count, scripted);
        const auto inputHash = Hash(layout.Serialize());
        const auto inputObjects = layout.objects.size();
        WorldState world(WorldDefinition::Compile(layout));
        RuntimeScripts runtime;
        std::string error;
        std::vector<double> samples, allocations, bytes;
        for (size_t frame = 0; frame < 72; ++frame)
        {
            Engine::CpuProfiler::Take();
            double elapsed;
            TestSupport::AllocationCounts memory;
            {
                TestSupport::AllocationProbe probe;
                const auto start = std::chrono::steady_clock::now();
                {
                    auto transaction = world.BeginTransaction();
                    auto candidate = runtime.BeginFrame(transaction);
                    if (!candidate.Run(ScriptPhase::Update, 1.0 / 60.0, error) || !candidate.Prepare(error) ||
                        !transaction.Prepare(error))
                        throw std::runtime_error(error);
                    if (!transaction.Commit() || !candidate.Commit())
                        throw std::runtime_error("Runtime measurement publication failed");
                    candidate.FinishCallbacks();
                }
                world.TakeChanges();
                elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                memory = probe.Counts();
            }
            if (frame >= 8)
            {
                samples.push_back(elapsed);
                allocations.push_back(static_cast<double>(memory.count));
                bytes.push_back(static_cast<double>(memory.bytes));
            }
        }
        layout = world.Read().ExportLayout();
        runtime.Stop(world);
        if (layout.objects.size() != inputObjects)
            throw std::runtime_error("Runtime measurement changed fixture structure");
        if (!scripted && Hash(layout.Serialize()) != inputHash)
            throw std::runtime_error("Static CPU fixture changed during update");
        std::sort(samples.begin(), samples.end());
        std::sort(allocations.begin(), allocations.end());
        std::sort(bytes.begin(), bytes.end());
        const auto percentile = [&](double fraction)
        {
            return samples[static_cast<size_t>((samples.size() - 1) * fraction)];
        };
        Engine::Json result{{"objects", count},
                            {"scripted", scripted},
                            {"inputHash", std::to_string(inputHash)},
                            {"resultHash", std::to_string(Hash(layout.Serialize()))},
                            {"warmupFrames", 8},
                            {"measuredFrames", samples.size()},
                            {"medianMs", percentile(.5)},
                            {"p95Ms", percentile(.95)},
                            {"p99Ms", percentile(.99)},
                            {"medianAllocations", allocations[(allocations.size() - 1) / 2]},
                            {"medianAllocatedBytes", bytes[(bytes.size() - 1) / 2]}};
        std::cout << result.dump() << '\n';
        return result;
    }
}

int main(int argc, char** argv)
{
    try
    {
        Engine::Json results{{"path", "RuntimeScripts::Frame + WorldState::Transaction"},
                             {"stepSeconds", 1.0 / 60.0},
                             {"scenarios", Engine::Json::array()}};
        for (const auto count : {size_t{0}, size_t{1000}, size_t{5000}, size_t{10000}})
            results["scenarios"].push_back(Measure(count, false));
        results["scenarios"].push_back(Measure(1000, true));
        if (argc > 1)
        {
            const auto path = std::filesystem::path(argv[1]);
            std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path);
            output << results.dump(2) << '\n';
            if (!output)
                throw std::runtime_error("Cannot save CPU measurement");
        }
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
