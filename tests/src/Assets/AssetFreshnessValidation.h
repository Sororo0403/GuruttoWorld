#pragma once
#include "../../../Editor/src/Assets/AssetChanges.h"
#include "../../../Editor/src/Assets/AssetDependencies.h"
#include "../../../Editor/src/Assets/SkeletonPreview.h"

namespace AssetFreshnessValidation
{
    /// <summary>日時を保持する更新・置換と、骨格プレビューの再試行・カタログ世代を検証します。</summary>
    inline void Run()
    {
        const auto require = [](bool value, const char* message)
        {
            if (!value)
                throw std::runtime_error(message);
        };
        const auto root = std::filesystem::absolute("generated/tests/asset-freshness") /
                          (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(root / "Assets/Models");
        std::filesystem::create_directories(root / "Assets/Scenes");
        const auto model = root / "Assets/Models/References.obj";
        Engine::FileContents::Write(model, "mtllib old.mtl\n");
        const auto time = std::filesystem::last_write_time(model);
        const auto first = Editor::AssetChanges::Capture(root);
        Editor::AssetChanges changes;
        changes.Observe(first);
        Editor::AssetDependencies dependencies;
        const Editor::ProjectAsset asset{"Assets/Models/References.obj", Editor::AssetKind::Model};
        const auto now = std::chrono::steady_clock::now();
        dependencies.Refresh(root, asset, now);
        require(dependencies.Values() == std::vector<std::string>{"old.mtl"}, "initial dependencies load");
        {
            std::ofstream output(model, std::ios::binary | std::ios::trunc);
            output << "mtllib new.mtl\n";
        }
        std::filesystem::last_write_time(model, time);
        const auto revised = Editor::AssetChanges::Capture(root);
        require(first != revised, "in-place edits with identical size and write time are observed");
        changes.Observe(revised);
        require(changes.Pending(), "same-time resource edit requests reload");
        changes.Observe(revised);
        require(changes.TakeReady(true), "stable resource edit reloads once");
        dependencies.Refresh(root, asset, now + std::chrono::seconds(1));
        require(dependencies.Values() == std::vector<std::string>{"new.mtl"} && dependencies.Reads() == 2,
                "same-time dependency edit replaces the preview");
        dependencies.Refresh(root, asset, now + std::chrono::seconds(2));
        require(dependencies.Reads() == 2, "unchanged dependency preview does not reparse");
        Engine::FileContents::Write(model, "mtllib old.mtl\n");
        std::filesystem::last_write_time(model, time);
        require(Editor::AssetChanges::Capture(root) != revised, "atomic replacement with restored time is observed");
        const auto beforeScene = Editor::AssetChanges::Capture(root);
        Engine::FileContents::Write(root / "Assets/Scenes/Edited.json", "{}");
        require(Editor::AssetChanges::Capture(root) == beforeScene, "scene JSON remains outside resource reload");

        const auto rigPath = root / "Assets/Models/Preview.gltf";
        const auto original = *Engine::FileContents::Read("Games/GuruttoWorld/Assets/Models/AnimatedBox.gltf");
        Engine::FileContents::Write(rigPath, original);
        const auto rigTime = std::filesystem::last_write_time(rigPath);
        Editor::SkeletonPreview preview;
        const auto rig = preview.Load(rigPath, 1, now);
        require(rig && !rig->clips.empty() && !rig->clips.front().name.empty() && preview.Load(rigPath, 1, now) == rig,
                "valid skeleton is reused");
        auto renamed = rig->clips.front().name;
        renamed.back() = renamed.back() == 'x' ? 'y' : 'x';
        auto source = original;
        const auto oldName = Engine::Json(rig->clips.front().name).dump();
        const auto position = source.find(oldName, source.find("\"animations\""));
        require(position != std::string::npos, "fixture clip name is present");
        source.replace(position, oldName.size(), Engine::Json(renamed).dump());
        require(source.size() == original.size(), "skeleton edit preserves byte count");
        Engine::FileContents::Write(rigPath, source);
        std::filesystem::last_write_time(rigPath, rigTime);
        const auto changed = preview.Load(rigPath, 1, now);
        require(changed && changed != rig && changed->clips.front().name == renamed,
                "same-time skeleton edit refreshes the preview");
        const auto rescanned = preview.Load(rigPath, 2, now);
        require(rescanned && rescanned != changed, "catalog changes invalidate external skeleton dependencies");

        const auto held = CreateFileW(rigPath.c_str(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(held != INVALID_HANDLE_VALUE, "skeleton read-lock fixture opens");
        Editor::SkeletonPreview transient;
        const auto failed = transient.Load(rigPath, 1, now);
        CloseHandle(held);
        require(!failed && !transient.Load(rigPath, 1, now + std::chrono::milliseconds(100)),
                "temporary import failure uses bounded retries");
        require(transient.Load(rigPath, 1, now + std::chrono::seconds(1)) != nullptr,
                "unchanged skeleton retries after a temporary read failure");
    }
}
