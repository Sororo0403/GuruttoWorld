#include "BuildPanel.h"

namespace Editor
{
    auto BuildPanel::RepositoryRoot(const std::filesystem::path& content) -> std::filesystem::path
    {
        if (std::filesystem::is_regular_file(content / "WP1.project.json"))
        {
            const auto context = Engine::ProjectContext::Load(content);
            context.RequireEngine(Engine::ProjectContext::SourceEngineRoot());
            return context.engineRoot;
        }
        auto path = std::filesystem::absolute(content).parent_path();
        while (!path.empty() && path != path.root_path())
        {
            if (std::filesystem::is_regular_file(path / "WP1.slnx"))
                return path;
            path = path.parent_path();
        }
        return content.parent_path();
    }

    auto BuildPanel::QuoteArgument(std::wstring_view argument) -> std::wstring
    {
        std::wstring result = L"\"";
        size_t slashes = 0;
        for (const auto character : argument)
        {
            if (character == L'\\')
            {
                ++slashes;
                continue;
            }
            result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            slashes = 0;
            result += character;
        }
        result.append(slashes * 2, L'\\');
        return result + L'"';
    }

    BuildPanel::~BuildPanel()
    {
        if (job_)
            CloseHandle(job_);
        if (process_)
            CloseHandle(process_);
    }

    auto BuildPanel::Poll(const std::filesystem::path& content) -> void
    {
        if (!process_)
            return;
        ReadProgress(content);
        DWORD code = STILL_ACTIVE;
        if (!GetExitCodeProcess(process_, &code) || code == STILL_ACTIVE)
            return;
        nextLog_ = 0;
        ReadProgress(content);
        CloseHandle(process_);
        process_ = nullptr;
        if (job_)
        {
            CloseHandle(job_);
            job_ = nullptr;
        }
        try
        {
            std::ifstream input(repository_ / Folder() / (runId_ + ".json"));
            if (!input)
                throw std::runtime_error("Build result missing; see log.");
            const auto result = Engine::Json::parse(input);
            if (result.at("success").get<bool>())
            {
                if (scripts_)
                    status_ = "ゲーム処理のコンパイルが完了しました。";
                else
                {
                    package_ = Engine::AssetDatabase::Path(result.at("package").get<std::string>());
                    status_ = "ビルド・パッケージ作成が完了しました。";
                }
                completed_ = true;
            }
            else
            {
                status_ = result.at("error").get<std::string>();
                completed_ = false;
            }
        }
        catch (const std::exception& error)
        {
            status_ = error.what();
            completed_ = false;
        }
    }

    auto BuildPanel::Cancel() -> void
    {
        if (!process_)
            return;
        if (!TerminateJobObject(job_, ERROR_CANCELLED))
        {
            status_ = "ビルドの停止に失敗しました。";
            return;
        }
        CloseHandle(process_);
        process_ = nullptr;
        CloseHandle(job_);
        job_ = nullptr;
        status_ = "ビルドをキャンセルしました。途中の出力とログは確認できます。";
    }

    auto BuildPanel::ReadProgress(const std::filesystem::path&) -> void
    {
        const auto now = GetTickCount64();
        if (now < nextLog_)
            return;
        nextLog_ = now + 500;
        try
        {
            std::ifstream progress(repository_ / Folder() / (runId_ + ".progress.json"));
            if (progress)
            {
                const auto value = Engine::Json::parse(progress);
                phase_ = value.value("phase", std::string{});
                step_ = value.value("step", 0);
            }
        }
        catch (...)
        {
        } // 進捗ファイルの書き込み途中で読み取れなくても、ビルドの監視は継続します。
        std::ifstream input(log_, std::ios::binary | std::ios::ate);
        if (input)
        {
            const auto bytes = input.tellg();
            if (bytes > 0)
            {
                input.seekg(bytes > 65536 ? bytes - std::streamoff(65536) : std::streampos(0));
                tail_.assign(std::istreambuf_iterator<char>(input), {});
            }
        }
    }

    auto BuildPanel::LaunchBuildProcess(const std::filesystem::path& executable, std::wstring command,
                                        const std::filesystem::path& repo) -> void
    {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION information{};
        auto job = CreateJobObjectW(nullptr, nullptr);
        if (!job)
            throw std::runtime_error("Cannot create build job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        {
            CloseHandle(job);
            throw std::runtime_error("Cannot configure build job");
        }
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, repo.c_str(), &startup, &information))
        {
            CloseHandle(job);
            throw std::runtime_error("Cannot start build process");
        }
        if (!AssignProcessToJobObject(job, information.hProcess) ||
            ResumeThread(information.hThread) == static_cast<DWORD>(-1))
        {
            TerminateProcess(information.hProcess, ERROR_CANCELLED);
            CloseHandle(information.hThread);
            CloseHandle(information.hProcess);
            CloseHandle(job);
            throw std::runtime_error("Cannot manage build process");
        }
        CloseHandle(information.hThread);
        process_ = information.hProcess;
        job_ = job;
        started_ = GetTickCount64();
        nextLog_ = 0;
        tail_.clear();
        phase_.clear();
        step_ = 0;
        status_ = "ビルド中です。エディターを終了するとビルドも停止します。";
    }

    auto BuildPanel::Start(const std::filesystem::path& content) -> void
    {
        if (process_)
            return;
        const auto repo = RepositoryRoot(content);
        std::array<wchar_t, 32768> module{};
        const auto length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        if (!length || length >= module.size())
            throw std::runtime_error("Cannot locate Editor.exe.");
        const auto executable = std::filesystem::path(module.data()).parent_path() / "ProjectTool.exe";
        if (!std::filesystem::is_regular_file(executable))
            throw std::runtime_error("ProjectTool.exe is missing. Rebuild the Editor.");
        runId_.clear();
        std::random_device random;
        constexpr char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < 32; ++i)
            runId_ += hex[random() % 16];
        const bool external = std::filesystem::is_regular_file(content / "WP1.project.json");
        repository_ = external ? content : repo;
        log_ = repository_ / Folder() / (runId_ + ".log");
        package_.clear();
        completed_.reset();
        const std::wstring configuration = scripts_ ? Engine::AssetDatabase::Path(scriptConfiguration_).wstring()
                                           : configuration_ == 0 ? L"Release"
                                                                 : L"Development";
        std::wstring command = QuoteArgument(executable.wstring()) + (scripts_ ? L" build-scripts" : L" build-player") +
                               L" --configuration " + configuration + L" --run-id " +
                               std::wstring(runId_.begin(), runId_.end());
        command += L" --content " + QuoteArgument(content.wstring()) + L" --work-root " +
                   QuoteArgument(repository_.wstring()) + L" --engine " + QuoteArgument(repo.wstring());
        if (external && !scripts_)
            command += L" --project " + QuoteArgument(content.wstring());
        LaunchBuildProcess(executable, std::move(command), repo);
    }

    auto BuildPanel::DrawProgressAndLogs() -> void
    {
        if (Running())
        {
            ImGui::Text("経過時間: %.0f秒", (GetTickCount64() - started_) / 1000.0);
            ImGui::ProgressBar(std::clamp(step_ / 4.0f, 0.0f, 1.0f), {0, 0},
                               phase_.empty() ? "開始準備" : phase_.c_str());
            if (ImGui::Button("ビルドをキャンセル###Cancel build"))
                Cancel();
        }
        if (!log_.empty())
            ImGui::TextWrapped("ログ: %s", Engine::AssetDatabase::Text(log_).c_str());
        if (!log_.empty() && ImGui::Button("ログのパスをコピー###Copy build log path"))
            ImGui::SetClipboardText(Engine::AssetDatabase::Text(log_).c_str());
        if (!package_.empty() && ImGui::Button("配布フォルダーのパスをコピー###Copy package path"))
            ImGui::SetClipboardText(Engine::AssetDatabase::Text(package_).c_str());
        if (!tail_.empty() &&
            ImGui::CollapsingHeader("ビルドログ（末尾）###Build log tail", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginChild("Build log", {0, 200}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
            {
                const bool follow = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1;
                ImGui::TextUnformatted(tail_.c_str());
                if (follow)
                    ImGui::SetScrollHereY(1);
            }
            ImGui::EndChild();
        }
        if (!package_.empty())
            ImGui::TextWrapped("配布フォルダー: %s", Engine::AssetDatabase::Text(package_).c_str());
    }

    auto BuildPanel::Draw(const std::filesystem::path& content, bool saved) -> void
    {
        if (!open)
            return;
        if (ImGui::Begin(scripts_ ? "ゲーム処理のコンパイル###Build scripts" : "ビルド###Build player", &open))
        {
            ImGui::TextWrapped(
                "%s",
                scripts_
                    ? "Assets/ScriptsのC++ゲーム処理をコンパイルします。再生中の結果は停止後に反映します。"
                    : "開いているゲームの保存済み素材とプレイヤーをビルドし、generated/buildsに新しい配布用フォルダーを作成します。");
            if (scripts_)
                ImGui::Text("構成: %s", scriptConfiguration_.c_str());
            else
            {
                ImGui::BeginDisabled(Running());
                ImGui::Combo("構成###Configuration", &configuration_, "Release\0Development\0");
                ImGui::EndDisabled();
            }
            if (!saved && !scripts_)
                ImGui::TextUnformatted("シーンを保存してからビルドしてください。");
            ImGui::BeginDisabled(Running() || (!saved && !scripts_));
            const bool start = ImGui::Button(scripts_ ? "コンパイル###Build and package"
                                                      : "ビルド・パッケージ作成###Build and package");
            ImGui::EndDisabled();
            if (start)
                try
                {
                    Start(content);
                }
                catch (const std::exception& error)
                {
                    status_ = error.what();
                }
            if (!status_.empty())
                ImGui::TextWrapped("%s", status_.c_str());
            DrawProgressAndLogs();
        }
        ImGui::End();
    }
}
