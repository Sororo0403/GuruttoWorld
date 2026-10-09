#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <imgui.h>
#include <optional>
#include <utility>
namespace Editor {
class BuildPanel final {
    HANDLE process_=nullptr;
    HANDLE job_=nullptr;
    std::string runId_,status_;
    std::filesystem::path package_,log_;
    int configuration_=0;
    ULONGLONG nextLog_=0,started_=0;
    std::string tail_,phase_;
    int step_=0;
    bool scripts_=false;
    std::string scriptConfiguration_="Development";
    std::optional<bool> completed_;
    const char* Folder() const { return scripts_ ? "generated/script-builds" : "generated/builds"; }
public:
    bool open=false;
    /// <summary>配布ビルドまたはC++ゲーム処理ビルドの操作画面を作ります。</summary>
    explicit BuildPanel(bool scripts=false,const char* configuration="Development") : scripts_(scripts),scriptConfiguration_(configuration) {}
    /// <summary>前回のビルド結果を一回だけ取得します。</summary>
    std::optional<bool> TakeCompleted() { return std::exchange(completed_,std::nullopt); }
    ~BuildPanel() { if(job_) CloseHandle(job_); if (process_) CloseHandle(process_); }
    bool Running() const { return process_!=nullptr; }
    void Poll(const std::filesystem::path& content) {
        if (!process_) return;
        ReadProgress(content);
        DWORD code=STILL_ACTIVE;
        if (!GetExitCodeProcess(process_,&code) || code==STILL_ACTIVE) return;
        nextLog_=0;ReadProgress(content);
        CloseHandle(process_); process_=nullptr;
        if(job_) {CloseHandle(job_);job_=nullptr;}
        try {
            std::ifstream input(content.parent_path()/Folder()/(runId_+".json"));
            if (!input) throw std::runtime_error("Build result missing; see log.");
            const auto result=Engine::Json::parse(input);
            if (result.at("success").get<bool>()) {
                if (scripts_) status_="ゲーム処理のコンパイルが完了しました。";
                else { package_=Engine::AssetDatabase::Path(result.at("package").get<std::string>()); status_="ビルド・パッケージ作成が完了しました。"; }
                completed_=true;
            } else { status_=result.at("error").get<std::string>(); completed_=false; }
        } catch (const std::exception& error) { status_=error.what(); completed_=false; }
    }
    void Cancel() {
        if(!process_) return;
        if(!TerminateJobObject(job_,ERROR_CANCELLED)) {status_="ビルドの停止に失敗しました。";return;}
        CloseHandle(process_); process_=nullptr; CloseHandle(job_);job_=nullptr;
        status_="ビルドをキャンセルしました。途中の出力とログは確認できます。";
    }
    void ReadProgress(const std::filesystem::path& content) {
        const auto now=GetTickCount64(); if(now<nextLog_) return;nextLog_=now+500;
        try {
            std::ifstream progress(content.parent_path()/Folder()/(runId_+".progress.json"));
            if(progress) {const auto value=Engine::Json::parse(progress);phase_=value.value("phase",std::string{});step_=value.value("step",0);}
        } catch(...) {} // A partially published status must not stop the build.
        std::ifstream input(log_,std::ios::binary|std::ios::ate);
        if(input) {
            const auto bytes=input.tellg();
            if(bytes>0) {input.seekg(bytes>65536?bytes-std::streamoff(65536):std::streampos(0));tail_.assign(std::istreambuf_iterator<char>(input),{});}
        }
    }
    void Start(const std::filesystem::path& content) {
        if (process_) return;
        const auto repo=content.parent_path(),script=repo/(scripts_ ? "scripts/BuildScripts.ps1" : "scripts/BuildPlayer.ps1");
        if (!std::filesystem::is_regular_file(script)) throw std::runtime_error("Build requires the source repository and Visual Studio.");
        runId_.clear(); std::random_device random; constexpr char hex[]="0123456789abcdef";
        for (size_t i=0;i<32;++i) runId_+=hex[random()%16];
        log_=repo/Folder()/(runId_+".log"); package_.clear(); completed_.reset();
        std::array<wchar_t,32768> system{}; const auto length=GetSystemDirectoryW(system.data(),static_cast<UINT>(system.size()));
        if (!length || length>=system.size()) throw std::runtime_error("Cannot locate PowerShell");
        const auto executable=std::filesystem::path(system.data())/"WindowsPowerShell/v1.0/powershell.exe";
        const std::wstring configuration=scripts_ ? Engine::AssetDatabase::Path(scriptConfiguration_).wstring() : configuration_==0 ? L"Release" : L"Development";
        std::wstring command=L"\""+executable.wstring()+L"\" -NoProfile -ExecutionPolicy Bypass -File \""+script.wstring()+L"\" -Configuration "+configuration+L" -RunId "+std::wstring(runId_.begin(),runId_.end());
        if (scripts_) command+=L" -Content \""+content.wstring()+L"\"";
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION information{};
        auto job=CreateJobObjectW(nullptr,nullptr);
        if(!job) throw std::runtime_error("Cannot create build job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {CloseHandle(job);throw std::runtime_error("Cannot configure build job");}
        if (!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,repo.c_str(),&startup,&information)) {CloseHandle(job);throw std::runtime_error("Cannot start build process");}
        if(!AssignProcessToJobObject(job,information.hProcess) || ResumeThread(information.hThread)==static_cast<DWORD>(-1)) {
            TerminateProcess(information.hProcess,ERROR_CANCELLED);CloseHandle(information.hThread);CloseHandle(information.hProcess);CloseHandle(job);
            throw std::runtime_error("Cannot manage build process");
        }
        CloseHandle(information.hThread); process_=information.hProcess;job_=job;started_=GetTickCount64();nextLog_=0;tail_.clear();phase_.clear();step_=0;
        status_="ビルド中です。エディターを終了するとビルドも停止します。";
    }
    void Draw(const std::filesystem::path& content,bool saved) {
        if (!open) return;
        if (ImGui::Begin(scripts_ ? "ゲーム処理のコンパイル###Build scripts" : "ビルド###Build player",&open)) {
            ImGui::TextWrapped("%s",scripts_ ? "Assets/ScriptsのC++ゲーム処理をコンパイルします。再生中の結果は停止後に反映します。" : "保存済みContentとAppをビルドし、generated/buildsに新しい配布用フォルダーを作成します。");
            if (scripts_) ImGui::Text("構成: %s",scriptConfiguration_.c_str());
            else { ImGui::BeginDisabled(Running()); ImGui::Combo("構成###Configuration",&configuration_,"Release\0Development\0"); ImGui::EndDisabled(); }
            if (!saved && !scripts_) ImGui::TextUnformatted("シーンを保存してからビルドしてください。");
            ImGui::BeginDisabled(Running() || (!saved && !scripts_));
            const bool start=ImGui::Button(scripts_ ? "コンパイル###Build and package" : "ビルド・パッケージ作成###Build and package"); ImGui::EndDisabled();
            if (start) try { Start(content); } catch (const std::exception& error) { status_=error.what(); }
            if (!status_.empty()) ImGui::TextWrapped("%s",status_.c_str());
            if(Running()) {
                ImGui::Text("経過時間: %.0f秒",(GetTickCount64()-started_)/1000.0);
                ImGui::ProgressBar(std::clamp(step_/4.0f,0.0f,1.0f),{0,0},phase_.empty()?"開始準備":phase_.c_str());
                if(ImGui::Button("ビルドをキャンセル###Cancel build")) Cancel();
            }
            if (!log_.empty()) ImGui::TextWrapped("ログ: %s",Engine::AssetDatabase::Text(log_).c_str());
            if(!log_.empty() && ImGui::Button("ログのパスをコピー###Copy build log path")) ImGui::SetClipboardText(Engine::AssetDatabase::Text(log_).c_str());
            if(!package_.empty() && ImGui::Button("配布フォルダーのパスをコピー###Copy package path")) ImGui::SetClipboardText(Engine::AssetDatabase::Text(package_).c_str());
            if(!tail_.empty() && ImGui::CollapsingHeader("ビルドログ（末尾）###Build log tail",ImGuiTreeNodeFlags_DefaultOpen)) {
                if(ImGui::BeginChild("Build log",{0,200},ImGuiChildFlags_Borders,ImGuiWindowFlags_HorizontalScrollbar)) {
                    const bool follow=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-1;
                    ImGui::TextUnformatted(tail_.c_str());if(follow) ImGui::SetScrollHereY(1);
                } ImGui::EndChild();
            }
            if (!package_.empty()) ImGui::TextWrapped("配布フォルダー: %s",Engine::AssetDatabase::Text(package_).c_str());
        } ImGui::End();
    }
};
}
