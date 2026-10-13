#pragma once
#include <filesystem>
#include <string>
namespace App
{
    struct GameProfileOptions
    {
        std::string scene;
        std::filesystem::path output;
        unsigned int frames = 600;
        bool simulateInactive = false;
        bool replayInput = false;
    };
    class Game final
    {
    public:
        /// <summary>
        /// Factory とシーン管理を構成し、指定時だけ負荷を記録してアプリケーションを実行します。
        /// </summary>
        static int Run(const GameProfileOptions* profile = nullptr);
    };
}
