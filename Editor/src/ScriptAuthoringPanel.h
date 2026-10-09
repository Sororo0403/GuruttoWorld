#pragma once
#include "BuildPanel.h"
#include <SceneRuntime/ScriptModule.h>
#include <shellapi.h>
#pragma comment(lib,"shell32.lib")

namespace Editor
{
    class ScriptAuthoringPanel final
    {
        using Inventory=std::map<std::filesystem::path,std::pair<std::filesystem::file_time_type,uintmax_t>>;
        Inventory inventory_;
        BuildPanel build_{true,SceneRuntime::ScriptModule::Configuration()};
        std::array<char,65> name_{};
        std::filesystem::path selected_;
        std::string status_;
        ULONGLONG nextScan_=0,changedAt_=0;
        bool initialized_=false,needsBuild_=false,needsInstall_=false,ready_=true;
        /// <summary>処理ソースと補助ヘッダーの変更を確認します。</summary>
        static Inventory Scan(const std::filesystem::path& content)
        {
            Inventory result; const auto folder=content/"Assets/Scripts";
            if (!std::filesystem::exists(folder)) return result;
            for (const auto& file:std::filesystem::recursive_directory_iterator(folder)) {
                if (!file.is_regular_file()) continue;
                const auto extension=file.path().extension();
                if (extension==".cpp" || extension==".h" || extension==".hpp") result[file.path()]={file.last_write_time(),file.file_size()};
            }
            return result;
        }
        /// <summary>変更の安定確認と初回コンパイルを予約します。</summary>
        void Observe(const std::filesystem::path& content,ULONGLONG now)
        {
            if (now<nextScan_) return;
            nextScan_=now+500; auto current=Scan(content);
            if (initialized_ && current==inventory_) return;
            needsBuild_=initialized_ || !current.empty() || std::filesystem::exists(content/"Assets/Scripts/Bin"/SceneRuntime::ScriptModule::Configuration()/"module.json");
            ready_=!needsBuild_; initialized_=true; inventory_=std::move(current); changedAt_=now;
        }
        /// <summary>ソースの選択と外部エディターでの編集を表示します。</summary>
        void DrawSources()
        {
            for (const auto& [path,stamp]:inventory_) {
                static_cast<void>(stamp);
                if (path.extension()==".cpp" && ImGui::Selectable(Engine::AssetDatabase::Text(path.filename()).c_str(),path==selected_)) selected_=path;
            }
            if (!selected_.empty() && ImGui::Button("ソースを開く###Open source")) {
                if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",selected_.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
                    status_="ソースを開けませんでした。C++エディターのファイル関連付けを確認してください。";
            }
        }
    public:
        bool open=false;
        /// <summary>コンパイル済みで、再読み込み待ちでない場合に再生を許可します。</summary>
        bool Ready() const { return ready_ && !needsBuild_ && !needsInstall_ && !build_.Running(); }
        /// <summary>変更が安定したソースをコンパイルし、停止中に結果を読み込みます。</summary>
        void Poll(const std::filesystem::path& content,bool editing)
        {
            build_.Poll(content);
            if (const auto completed=build_.TakeCompleted()) {
                if (*completed) needsInstall_=true;
                else { ready_=false; build_.open=true; }
            }
            try {
                const auto now=GetTickCount64();
                Observe(content,now);
                if (needsBuild_ && !build_.Running() && now-changedAt_>=1000) {
                    build_.Start(content); needsBuild_=false; ready_=false;
                }
                if (needsInstall_ && editing && !build_.Running() && !needsBuild_) {
                    ready_=SceneRuntime::ScriptModule::Reload(content,status_); needsInstall_=false;
                    if (ready_) status_="ゲーム処理を再読み込みしました。Inspectorの追加メニューから選択できます。";
                }
            } catch (const std::exception& error) { status_=error.what(); ready_=false; needsBuild_=false; }
        }
        /// <summary>処理の作成、ソース編集、コンパイル診断を表示します。</summary>
        void Draw(const std::filesystem::path& content)
        {
            const bool shown=open;
            if (shown && ImGui::Begin("C++ゲーム処理###Script authoring",&open)) {
                ImGui::InputText("処理名（英数字・_）###Script name",name_.data(),name_.size());
                if (ImGui::Button("処理を作成###Create script")) {
                    if (SceneRuntime::ScriptModule::CreateSource(content,name_.data(),selected_,status_)) {
                        status_="処理ソースを作成しました。保存した変更は自動でコンパイルされます。"; nextScan_=0;
                    }
                }
                ImGui::Separator();
                DrawSources();
                if (ImGui::Button("コンパイルと診断###Compiler diagnostics")) build_.open=true;
                if (build_.Running()) ImGui::TextUnformatted("コンパイル中です。");
                if (needsInstall_) ImGui::TextUnformatted("再生を停止すると新しい処理を読み込みます。");
                if (!status_.empty()) ImGui::TextWrapped("%s",status_.c_str());
                ImGui::TextUnformatted("Start / Update / FixedUpdate / LateUpdate / Event / Stopと公開データをC++で定義できます。");
            }
            if (shown) ImGui::End();
            build_.Draw(content,true);
        }
    };
}
