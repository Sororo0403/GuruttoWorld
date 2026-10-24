#include <SceneRuntime/Scene/Prefab.h>
#include <chrono>
#include <iomanip>
#include <iostream>

namespace
{
    using namespace SceneRuntime;
    SceneLayout Leaf()
    {
        SceneLayout layout;
        for (size_t i = 0; i < 8; ++i)
        {
            ScenePlacement object;
            object.id = "leaf-" + std::to_string(i);
            if (i != 0)
                object.parentId = "leaf-0";
            layout.objects.push_back(std::move(object));
        }
        return layout;
    }
    void Measure(size_t instances)
    {
        const std::filesystem::path leafPath = "Assets/Prefabs/Leaf.prefab";
        const std::filesystem::path parentPath = "Assets/Prefabs/Parent.prefab";
        auto leaf = Leaf();
        SceneLayout parent;
        ScenePlacement root;
        root.id = "parent";
        parent.objects.push_back(root);
        for (size_t i = 0; i < 4; ++i)
        {
            const auto id = Prefab::Instantiate(parent, leaf, leafPath, {});
            for (auto& object : parent.objects)
                if (object.id == id)
                    object.parentId = root.id;
        }
        SceneLayout scene;
        for (size_t i = 0; i < instances; ++i)
            Prefab::Instantiate(scene, parent, parentPath, {});
        size_t reads = 0;
        const auto loader = [&](const std::filesystem::path& path)
        {
            ++reads;
            return path == leafPath ? leaf : parent;
        };
        Prefab::RefreshPrepared(scene, loader);
        reads = 0;
        constexpr size_t Repeats = 10;
        const auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < Repeats; ++i)
            Prefab::RefreshPrepared(scene, loader);
        const auto milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (reads != Repeats * 2)
            throw std::runtime_error("Dependency reads scale with instances");
        std::cout << instances << ',' << scene.objects.size() << ',' << Repeats << ',' << reads / Repeats << ','
                  << milliseconds / Repeats << '\n';
    }
}
int main()
{
    try
    {
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "instances,objects,repeats,dependency_reads_per_refresh,refresh_ms\n";
        for (const size_t count : {size_t{1}, size_t{8}, size_t{32}})
            Measure(count);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
