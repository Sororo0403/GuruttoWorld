#pragma once
#include "ProjectCatalog.h"
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/Resources/RenderTexture.h>
#include <Engine/Graphics/Resources/Texture2D.h>
#include <Engine/Graphics/Models/Object3D.h>
#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <Engine/Graphics/Camera.h>
#include <imgui.h>
#include <cmath>

namespace Editor
{
    // UI requests resources; Prepare owns uploads and replacement outside Render after GPU idle.
    class AssetPreview final
    {
    public:
        void Request(const ProjectAsset& asset)
        {
            if (selected_!=asset.path) { selected_=asset.path; kind_=asset.kind; requested_=true; }
            visible_=asset.kind==AssetKind::Model;
        }
        bool Ready() const { return loaded_==selected_ && !loaded_.empty(); }
        const std::string& Error() const { return error_; }
        void Invalidate() { requested_=true; }
        bool Prepare(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root);
        bool Render(ID3D12GraphicsCommandList* commands);
        void Draw(const ProjectAsset& asset);
    private:
        bool Load(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root);
        void DrawModel();
        void UpdateCamera();
        std::filesystem::path selected_, loaded_;
        AssetKind kind_=AssetKind::Model;
        bool requested_=false, visible_=false;
        std::string error_;
        std::unique_ptr<Engine::Texture2D> image_;
        Engine::Object3D model_;
        Engine::RenderTexture target_;
        Engine::Camera camera_;
        UINT64 imageId_=0, modelId_=0;
        float yaw_=0.4f, pitch_=0.2f, zoom_=1.0f, radius_=1.0f;
        std::array<float,3> center_{};
    };
}
