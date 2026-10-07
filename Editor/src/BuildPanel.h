#pragma once
#include <Engine/Assets/AssetDatabase.h>
#include <imgui.h>
namespace Editor {
class BuildPanel final {
    HANDLE process_=nullptr;
    std::string runId_,status_;
    std::filesystem::path package_,log_;
    int configuration_=0;
public:
    bool open=false;
    ~BuildPanel() { if (process_) CloseHandle(process_); }
    bool Running() const { return process_!=nullptr; }
    void Poll(const std::filesystem::path& content) {
        if (!process_) return;
        DWORD code=STILL_ACTIVE;
        if (!GetExitCodeProcess(process_,&code) || code==STILL_ACTIVE) return;
        CloseHandle(process_); process_=nullptr;
        try {
            std::ifstream input(content.parent_path()/"generated/builds"/(runId_+".json"));
            if (!input) throw std::runtime_error("Build result missing; see log.");
            const auto result=Engine::Json::parse(input);
            if (result.at("success").get<bool>()) {
                package_=Engine::AssetDatabase::Path(result.at("package").get<std::string>()); status_="ビルド・パッケージ作成が完了しました。";
            } else status_=result.at("error").get<std::string>();
        } catch (const std::exception& error) { status_=error.what(); }
    }
    void Start(const std::filesystem::path& content) {
        if (process_) return;
        const auto repo=content.parent_path(),script=repo/"scripts/BuildPlayer.ps1";
        if (!std::filesystem::is_regular_file(script)) throw std::runtime_error("Build requires the source repository and Visual Studio.");
        runId_.clear(); std::random_device random; constexpr char hex[]="0123456789abcdef";
        for (size_t i=0;i<32;++i) runId_+=hex[random()%16];
        log_=repo/"generated/builds"/(runId_+".log"); package_.clear();
        std::array<wchar_t,32768> system{}; const auto length=GetSystemDirectoryW(system.data(),static_cast<UINT>(system.size()));
        if (!length || length>=system.size()) throw std::runtime_error("Cannot locate PowerShell");
        const auto executable=std::filesystem::path(system.data())/"WindowsPowerShell/v1.0/powershell.exe";
        const std::wstring configuration=configuration_==0 ? L"Release" : L"Development";
        std::wstring command=L"\""+executable.wstring()+L"\" -NoProfile -ExecutionPolicy Bypass -File \""+script.wstring()+L"\" -Configuration "+configuration+L" -RunId "+std::wstring(runId_.begin(),runId_.end());
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION information{};
        if (!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,repo.c_str(),&startup,&information)) throw std::runtime_error("Cannot start build process");
        CloseHandle(information.hThread); process_=information.hProcess; status_="ビルド中です。進捗はログへ記録します。";
    }
    void Draw(const std::filesystem::path& content,bool saved) {
        if (!open) return;
        if (ImGui::Begin("ビルド###Build player",&open)) {
            ImGui::TextWrapped("保存済みContentとAppをビルドし、generated/buildsに新しい配布用フォルダーを作成します。");
            ImGui::BeginDisabled(Running()); ImGui::Combo("構成###Configuration",&configuration_,"Release\0Development\0"); ImGui::EndDisabled();
            if (!saved) ImGui::TextUnformatted("シーンを保存してからビルドしてください。");
            ImGui::BeginDisabled(Running() || !saved);
            const bool start=ImGui::Button("ビルド・パッケージ作成###Build and package"); ImGui::EndDisabled();
            if (start) try { Start(content); } catch (const std::exception& error) { status_=error.what(); }
            if (!status_.empty()) ImGui::TextWrapped("%s",status_.c_str());
            if (!log_.empty()) ImGui::TextWrapped("ログ: %s",Engine::AssetDatabase::Text(log_).c_str());
            if (!package_.empty()) ImGui::TextWrapped("配布フォルダー: %s",Engine::AssetDatabase::Text(package_).c_str());
        } ImGui::End();
    }
};
}
