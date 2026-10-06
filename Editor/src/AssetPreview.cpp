#include "AssetPreview.h"
#include <algorithm>
#include <stdexcept>

namespace Editor
{
    bool AssetPreview::Prepare(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        if (!requested_ || selected_.empty()) return true;
        requested_=false;
        if (!renderer.WaitForIdle()) return false;
        try
        {
            if (!Load(renderer,root)) throw std::runtime_error("Preview could not be loaded. See Console for details.");
            loaded_=selected_;
            error_.clear();
        }
        catch (const std::exception& exception) { error_=exception.what(); }
        return true;
    }

    bool AssetPreview::Load(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root)
    {
        if (selected_.is_absolute() || std::any_of(selected_.begin(),selected_.end(),
            [](const auto& part) { return part==".."; })) return false;
        if (kind_==AssetKind::Texture)
        {
            auto candidate=std::make_unique<Engine::Texture2D>();
            if (!candidate->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),root/selected_)) return false;
            const auto id=renderer.SetSceneTexture(candidate->GetShaderResourceView(),3).ptr;
            if (!id) return false;
            image_=std::move(candidate); imageId_=id;
            model_.SetModel({});
            return true;
        }
        if (kind_!=AssetKind::Model) { image_.reset(); model_.SetModel({}); return true; }
        auto candidate=std::make_shared<Engine::ModelRenderer>();
        if (!candidate->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),root/selected_,root/"Shaders/Mesh.hlsl")) return false;
        if (!target_.GetResource() && !target_.Resize(renderer,512,512)) return false;
        const auto id=renderer.SetSceneTexture(target_.GetShaderResourceView(),2).ptr;
        if (!id) return false;
        const auto& bounds=candidate->Bounds();
        center_={bounds.Center.x,bounds.Center.y,bounds.Center.z};
        radius_=std::max(0.01f,std::sqrt(bounds.Extents.x*bounds.Extents.x+
            bounds.Extents.y*bounds.Extents.y+bounds.Extents.z*bounds.Extents.z));
        model_.SetModel(std::move(candidate)); modelId_=id; image_.reset();
        yaw_=0.4f; pitch_=0.2f; zoom_=1.0f;
        UpdateCamera();
        return true;
    }

    void AssetPreview::UpdateCamera()
    {
        const float distance=radius_*3.0f*zoom_;
        const float horizontal=std::cos(pitch_);
        camera_.SetPosition({center_[0]-std::sin(yaw_)*horizontal*distance,
            center_[1]-std::sin(pitch_)*distance,center_[2]-std::cos(yaw_)*horizontal*distance});
        camera_.SetRotation(yaw_,pitch_);
        camera_.SetPerspective(DirectX::XM_PIDIV4,1.0f,std::max(0.001f,radius_*0.001f),radius_*100.0f);
    }

    bool AssetPreview::Render(ID3D12GraphicsCommandList* commands)
    {
        if (!visible_ || loaded_!=selected_ || kind_!=AssetKind::Model || !model_.GetModel()) return true;
        if (!target_.Begin(commands,{0.12f,0.14f,0.18f,1})) return false;
        model_.Draw(commands,camera_,Engine::DirectionalLight{});
        return target_.End(commands);
    }

    void AssetPreview::Draw(const ProjectAsset& asset)
    {
        Request(asset);
        if (requested_ && Ready()) ImGui::TextUnformatted("Preview update queued until editing.");
        if (!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
        if (!Ready()) { ImGui::TextUnformatted(error_.empty() ? "Loading preview..." : "Preview unavailable."); return; }
        if (asset.kind==AssetKind::Texture && image_)
        {
            ImGui::Text("Image: %u x %u (%s, RGBA8 preview)",image_->GetWidth(),image_->GetHeight(),
                ProjectCatalog::Text(asset.path.extension()).c_str());
            const float width=std::max(1.0f,ImGui::GetContentRegionAvail().x);
            const float scale=std::min(width/static_cast<float>(image_->GetWidth()),320.0f/static_cast<float>(image_->GetHeight()));
            ImGui::Image(static_cast<ImTextureID>(imageId_),ImVec2(image_->GetWidth()*scale,image_->GetHeight()*scale));
        }
        else if (asset.kind==AssetKind::Model && model_.GetModel()) DrawModel();
    }

    void AssetPreview::DrawModel()
    {
        const float size=std::clamp(ImGui::GetContentRegionAvail().x,1.0f,320.0f);
        ImGui::Image(static_cast<ImTextureID>(modelId_),ImVec2(size,size));
        if (ImGui::IsItemHovered())
        {
            const auto& input=ImGui::GetIO();
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            { yaw_+=input.MouseDelta.x*0.01f; pitch_=std::clamp(pitch_+input.MouseDelta.y*0.01f,-1.4f,1.4f); }
            zoom_=std::clamp(zoom_*std::exp(-input.MouseWheel*0.1f),0.5f,10.0f);
            UpdateCamera();
        }
        ImGui::TextUnformatted("Drag to orbit / wheel to zoom");
        if (ImGui::Button("Reset preview camera")) { yaw_=0.4f; pitch_=0.2f; zoom_=1.0f; UpdateCamera(); }
    }
}
