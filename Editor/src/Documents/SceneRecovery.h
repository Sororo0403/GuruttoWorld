#pragma once
#include "../Project/ProjectCatalog.h"
#include <SceneRuntime/Scene/SceneLayout.h>
#include <fstream>
#include <cmath>
#include <Windows.h>

namespace Editor
{
    class SceneRecovery final
    {
    public:
        /// <summary>対象のパスを取得します。</summary>
        static std::filesystem::path Path(const std::filesystem::path& directory, const std::filesystem::path& scene);
        /// <summary>内容を書き込みます。</summary>
        static void Write(const std::filesystem::path& directory, const std::filesystem::path& scene,
                          const std::string& json);
        /// <summary>内容を読み取ります。</summary>
        static SceneRuntime::SceneLayout Read(const std::filesystem::path& directory,
                                              const std::filesystem::path& scene);
        /// <summary>保持する状態を消去します。</summary>
        void Clear(const std::filesystem::path& directory, const std::filesystem::path& scene);
        /// <summary>実行状態と進捗を取得します。</summary>
        void Poll(const std::filesystem::path& directory, const std::filesystem::path& scene, const std::string& json,
                  bool dirty, double seconds);
        bool Available() const
        {
            return available_;
        }
        bool PreviousSession() const
        {
            return available_ && last_.empty();
        }
        const std::string& Error() const
        {
            return error_;
        }

    private:
        std::filesystem::path scene_;
        std::string last_, error_;
        double elapsed_ = 0, check_ = 0;
        bool available_ = false;
    };
}
