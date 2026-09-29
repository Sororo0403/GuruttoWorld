#include <Engine/Scenes/SceneManager.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Input/Keyboard.h>
#include <Engine/Platform/Window.h>
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    struct Counts { int initialized = 0; int updated = 0; int drawn = 0; int destroyed = 0; };
    class TestScene final : public Engine::IScene
    {
    public:
        TestScene(Counts& counts, bool fail) : counts_(counts), fail_(fail) {}
        ~TestScene() override { ++counts_.destroyed; }
        bool Initialize(Engine::DirectX12Renderer&) override { ++counts_.initialized; return !fail_; }
        std::string Update(double, const Engine::Keyboard&) override { ++counts_.updated; return "Second"; }
        Engine::RenderResult Draw(Engine::DirectX12Renderer&) override { ++counts_.drawn; return Engine::RenderResult::Presented; }
    private:
        Counts& counts_;
        bool fail_;
    };
    class TestFactory final : public Engine::ISceneFactory
    {
    public:
        Counts counts;
        std::unique_ptr<Engine::IScene> Create(std::string_view name) override
        {
            if (name == "Missing") return {};
            return std::make_unique<TestScene>(counts, name == "Failed");
        }
    };
}
int main()
{
    try
    {
        Engine::Window window;
        Engine::DirectX12Renderer renderer;
        Check(window.Create(L"Scene validation"), "window");
        Check(renderer.Initialize(window.GetHandle()), "renderer");
        Engine::Keyboard keyboard;
        TestFactory factory;
        {
            Engine::SceneManager manager(factory);
            Check(!manager.RequestChange(""), "empty rejected");
            Check(manager.RequestChange("First"), "request first");
            Check(!manager.RequestChange("Second"), "duplicate pending rejected");
            Check(factory.counts.initialized == 0, "deferred construction");
            Check(manager.Draw(renderer) == Engine::RenderResult::Presented, "first draw");
            Check(manager.GetActiveName() == "First" && factory.counts.drawn == 1, "first active");
            manager.Update(0.016, keyboard);
            Check(factory.counts.destroyed == 0 && manager.GetActiveName() == "First", "not destroyed during update");
            manager.Update(0.016, keyboard);
            Check(factory.counts.updated == 1, "pending skips further update");
            Check(manager.Draw(renderer) == Engine::RenderResult::Presented, "transition draw");
            Check(manager.GetActiveName() == "Second" && factory.counts.destroyed == 1, "old scene released");
            Check(manager.RequestChange("Missing"), "unknown request");
            Check(manager.Draw(renderer) == Engine::RenderResult::Failed, "unknown rejected");
            Check(manager.GetActiveName() == "Second" && factory.counts.destroyed == 1, "old retained on unknown");
            Check(manager.RequestChange("Failed"), "failed init request");
            Check(manager.Draw(renderer) == Engine::RenderResult::Failed, "init failure reported");
            Check(manager.GetActiveName() == "Second" && factory.counts.destroyed == 2, "failed candidate released, old retained");
            Check(manager.Draw(renderer) == Engine::RenderResult::Presented, "old scene remains usable");
            Check(renderer.WaitForIdle(), "idle before shutdown");
        }
        Check(factory.counts.destroyed == 3, "current scene destroyed on shutdown");
        std::cout << "PASS: factory dispatch, deferred transition, lifecycle, unknown scene and init failure\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
