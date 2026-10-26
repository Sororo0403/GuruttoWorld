#include "../../../Editor/src/Documents/SceneDocument.h"
#include <Engine/Core/FileContents.h>
#include <chrono>
#include <iostream>

#if defined(__d3d12_h__)
#error Document storage must not include the rendering API
#endif

namespace
{
    size_t contracts = 0;

    void Require(bool value, const char* name)
    {
        ++contracts;
        if (!value)
            throw std::runtime_error(name);
        std::cout << "PASS: " << name << '\n';
    }

    void Write(const std::filesystem::path& path, const std::string& text)
    {
        Engine::FileContents::Write(path, text);
    }

    void Validate()
    {
        const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto root = std::filesystem::absolute("generated/tests/headless-document/" + nonce);
        std::filesystem::create_directories(root);
        const auto path = root / "scene.json";
        Editor::SceneDocument document(path);
        std::string error;
        Require(document.Apply(Editor::SceneDocument::PrepareOpen(path, true), error) && document.UnsavedNew(),
                "headless-document-create");
        SceneRuntime::ScenePlacement object;
        object.id = "missing-model";
        object.name = "Repairable";
        object.meshRenderer = SceneRuntime::MeshRendererComponent{};
        object.meshRenderer->model = "Assets/Models/Missing.glb";
        Require(document.Data()->Edit(SceneRuntime::AddObjectEdit{object}, error).succeeded,
                "headless-document-add-missing-reference");
        document.Save(document.Layout());
        Require(!document.UnsavedNew() && Engine::FileContents::Read(path).has_value(), "headless-document-save");
        Editor::SceneDocument reopened(root / "other.json");
        Require(reopened.Open(path, error) && reopened.Layout().objects.size() == 1 && reopened.Path() == path,
                "headless-document-open");
        auto components = reopened.Layout().objects.front();
        components.meshRenderer.reset();
        Require(reopened.Data()->Edit(SceneRuntime::ComponentsEdit{{components}}, error).succeeded,
                "headless-document-repair");
        reopened.Save(reopened.Layout());
        Require(!SceneRuntime::SceneLayout::Load(path).objects.front().meshRenderer, "headless-document-save-repair");
        const auto beforeFailure = reopened.Layout().Serialize();
        Write(root / "invalid.json", "{invalid");
        Require(!reopened.Open(root / "invalid.json", error) && reopened.Path() == path &&
                    reopened.Layout().Serialize() == beforeFailure,
                "headless-document-open-failure-retains-state");
        const auto external = *Engine::FileContents::Read(path) + "\n";
        Write(path, external);
        bool conflict = false;
        try
        {
            reopened.Save(reopened.Layout());
        }
        catch (const std::exception&)
        {
            conflict = true;
        }
        Require(conflict && *Engine::FileContents::Read(path) == external && reopened.Path() == path,
                "headless-document-save-rejects-external-edit");
        const auto destination = root / "saved-as.json";
        reopened.SaveAs(reopened.Layout(), destination, false);
        Require(reopened.Path() == destination && Engine::FileContents::Read(destination).has_value(),
                "headless-document-save-as");
        auto candidate = Editor::SceneDocument::PrepareOpen(path);
        Write(path, external + "\n");
        Require(reopened.Apply(std::move(candidate), error), "headless-document-apply-captured-source");
        conflict = false;
        try
        {
            reopened.Save(reopened.Layout());
        }
        catch (const std::exception&)
        {
            conflict = true;
        }
        Require(conflict, "headless-document-open-source-is-save-baseline");
    }
}

int main()
{
    try
    {
        Validate();
        std::cout << "Document storage contracts: " << contracts << ", failures: 0\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
