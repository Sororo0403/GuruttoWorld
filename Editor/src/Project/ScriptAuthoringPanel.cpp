#include "ScriptAuthoringPanel.h"

namespace Editor
{
    auto ScriptAuthoringPanel::Sources(const Inventory& inventory) -> Inventory
    {
        Inventory result;
        for (const auto& [path, stamp] : inventory)
            if (!path.filename().string().ends_with(".schema.json"))
                result.emplace(path, stamp);
        return result;
    }

    auto ScriptAuthoringPanel::Scan(const std::filesystem::path& content) -> Inventory
    {
        Inventory result;
        const auto folder = content / "Assets/Scripts";
        if (!std::filesystem::exists(folder))
            return result;
        for (const auto& file : std::filesystem::recursive_directory_iterator(folder))
        {
            if (!file.is_regular_file())
                continue;
            const auto extension = file.path().extension();
            if (extension == ".cpp" || extension == ".h" || extension == ".hpp" ||
                file.path().filename().string().ends_with(".schema.json"))
                result[file.path()] = AssetRevision::Capture(file.path());
        }
        return result;
    }

    auto ScriptAuthoringPanel::Observe(const std::filesystem::path& content, ULONGLONG now) -> void
    {
        if (now < nextScan_)
            return;
        nextScan_ = now + 500;
        auto current = Scan(content);
        if (initialized_ && current == inventory_)
            return;
        if (!initialized_ || Sources(current) != Sources(inventory_))
        {
            needsBuild_ = initialized_ || !Sources(current).empty() ||
                          std::filesystem::exists(content / "Assets/Scripts/Bin" /
                                                  SceneRuntime::ScriptModule::Configuration() / "module.json");
            needsInstall_ = !needsBuild_ && !current.empty();
            ready_ = !needsBuild_ && !needsInstall_;
        }
        else
        {
            needsInstall_ = !buildFailed_;
            ready_ = false;
            installFailed_ = false;
        }
        initialized_ = true;
        inventory_ = std::move(current);
        changedAt_ = now;
    }

    auto ScriptAuthoringPanel::DrawSources() -> void
    {
        for (const auto& [path, stamp] : inventory_)
        {
            static_cast<void>(stamp);
            const bool editable = path.extension() == ".cpp" || path.filename().string().ends_with(".schema.json");
            if (editable && ImGui::Selectable(Engine::AssetDatabase::Text(path.filename()).c_str(), path == selected_))
                selected_ = path;
        }
        if (!selected_.empty() && ImGui::Button("ソースを開く###Open source"))
        {
            if (reinterpret_cast<INT_PTR>(
                    ShellExecuteW(nullptr, L"open", selected_.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                status_ = "ソースを開けませんでした。ファイルの関連付けを確認してください。";
        }
    }

    auto ScriptAuthoringPanel::TakeReloaded() -> bool
    {
        const bool reloaded = reloaded_;
        reloaded_ = false;
        return reloaded;
    }

    auto ScriptAuthoringPanel::Poll(const std::filesystem::path& content, bool editing) -> void
    {
        build_.Poll(content);
        if (const auto completed = build_.TakeCompleted())
        {
            buildFailed_ = !*completed;
            if (*completed)
                needsInstall_ = true;
            else
            {
                needsInstall_ = false;
                ready_ = false;
                build_.open = true;
                status_ = "最新のゲーム処理のコンパイルに失敗しました。診断を確認して修正してください。";
            }
        }
        try
        {
            const auto now = GetTickCount64();
            Observe(content, now);
            if (needsBuild_ && !build_.Running() && now - changedAt_ >= 1000)
            {
                build_.Start(content);
                needsInstall_ = false;
                needsBuild_ = false;
                ready_ = false;
                buildFailed_ = false;
                installFailed_ = false;
            }
            if (needsInstall_ && editing && !build_.Running() && !needsBuild_ && !buildFailed_ &&
                now - changedAt_ >= 1000)
            {
                ready_ = SceneRuntime::ScriptModule::Reload(content, status_);
                needsInstall_ = false;
                installFailed_ = !ready_;
                if (ready_)
                {
                    reloaded_ = true;
                    status_ = "ゲーム処理を再読み込みしました。Inspectorの追加メニューから選択できます。";
                }
            }
        }
        catch (const std::exception& error)
        {
            status_ = error.what();
            ready_ = false;
            needsBuild_ = false;
        }
    }

    auto ScriptAuthoringPanel::Draw(const std::filesystem::path& content) -> void
    {
        const bool shown = open;
        if (shown && ImGui::Begin("C++ゲーム処理###Script authoring", &open))
        {
            ImGui::InputText("処理名（英数字・_）###Script name", name_.data(), name_.size());
            if (ImGui::Button("処理を作成###Create script"))
            {
                if (SceneRuntime::ScriptModule::CreateSource(content, name_.data(), selected_, status_))
                {
                    status_ = "処理ソースを作成しました。保存した変更は自動でコンパイルされます。";
                    nextScan_ = 0;
                }
            }
            ImGui::Separator();
            DrawSources();
            if (ImGui::Button("コンパイルと診断###Compiler diagnostics"))
                build_.open = true;
            if (build_.Running())
                ImGui::TextUnformatted("コンパイル中です。");
            if (needsInstall_)
                ImGui::TextUnformatted("再生を停止すると新しい処理を読み込みます。");
            if (!status_.empty())
                ImGui::TextWrapped("%s", status_.c_str());
            ImGui::TextUnformatted("処理はC++、公開データはJSONで定義します。JSONの変更は停止中に再読み込みします。");
        }
        if (shown)
            ImGui::End();
        build_.Draw(content, true);
    }
}
