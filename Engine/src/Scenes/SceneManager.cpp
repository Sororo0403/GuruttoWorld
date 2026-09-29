#include <Engine/Scenes/SceneManager.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <utility>

namespace Engine
{
    SceneManager::SceneManager(ISceneFactory& factory) : factory_(factory) {}
    bool SceneManager::RequestChange(std::string name)
    {
        if (name.empty() || !pendingName_.empty()) return false;
        pendingName_ = std::move(name);
        return true;
    }
    void SceneManager::Update(double deltaSeconds, const Keyboard& keyboard)
    {
        if (current_ && pendingName_.empty())
        {
            auto next = current_->Update(deltaSeconds, keyboard);
            if (!next.empty()) RequestChange(std::move(next));
        }
    }
    bool SceneManager::ApplyPending(DirectX12Renderer& renderer)
    {
        if (pendingName_.empty()) return true;
        const auto name = std::exchange(pendingName_, {});
        auto next = factory_.Create(name);
        if (!next)
        {
            Log::Error("Unknown scene: " + name);
            return false;
        }
        // 旧シーンの描画命令が完了してから生成・交換します。
        if (!renderer.WaitForIdle() || !next->Initialize(renderer)) return false;
        current_ = std::move(next);
        activeName_ = name;
        Log::Info("Scene changed: " + name);
        return true;
    }
    RenderResult SceneManager::Draw(DirectX12Renderer& renderer)
    {
        if (!ApplyPending(renderer) || !current_) return RenderResult::Failed;
        return current_->Draw(renderer);
    }
    const std::string& SceneManager::GetActiveName() const { return activeName_; }
}
