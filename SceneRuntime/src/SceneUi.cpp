#include <SceneRuntime/SceneUi.h>
#include <SceneRuntime/SceneCanvas.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <winrt/base.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_set>
#pragma comment(lib,"gdi32.lib")
namespace {
std::shared_ptr<Engine::Texture2D> TextTexture(const Engine::DirectX12Renderer& renderer,const SceneRuntime::TextComponent& text,const std::array<float,2>& size) {
    const UINT width=static_cast<UINT>(std::clamp(size[0],1.0f,4096.0f)),height=static_cast<UINT>(std::clamp(size[1],1.0f,4096.0f));
    HDC dc=CreateCompatibleDC(nullptr);
    if(!dc) throw std::runtime_error("Cannot create text device context");
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-static_cast<LONG>(height);
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* data=nullptr; HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);
    HFONT font=CreateFontW(-static_cast<int>(text.fontSize),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,winrt::to_hstring(text.font).c_str());
    if(!bitmap || !font) {if(bitmap) DeleteObject(bitmap); if(font) DeleteObject(font); DeleteDC(dc); throw std::runtime_error("Cannot create UI font bitmap");}
    const auto oldBitmap=SelectObject(dc,bitmap),oldFont=SelectObject(dc,font);
    std::memset(data,0,size_t(width)*height*4); SetTextColor(dc,RGB(255,255,255)); SetBkMode(dc,TRANSPARENT);
    RECT rect{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
    const auto content=winrt::to_hstring(text.text);
    DrawTextW(dc,content.c_str(),static_cast<int>(content.size()),&rect,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
    GdiFlush();
    std::vector<unsigned char> pixels(size_t(width)*height*4);
    const auto* source=static_cast<const unsigned char*>(data);
    for(size_t i=0;i<pixels.size();i+=4) {pixels[i]=pixels[i+1]=pixels[i+2]=255; pixels[i+3]=source[i];}
    SelectObject(dc,oldFont); SelectObject(dc,oldBitmap); DeleteObject(font); DeleteObject(bitmap); DeleteDC(dc);
    auto texture=std::make_shared<Engine::Texture2D>();
    if(!texture->InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),width,height,std::move(pixels))) throw std::runtime_error("Cannot upload UI text");
    return texture;
}
std::string Signature(const SceneRuntime::ScenePlacement& p,bool text) {
    if(text) return "text/"+p.text->text+"\n"+p.text->font+"\n"+std::to_string(p.text->fontSize)+"/"+std::to_string(p.rectTransform?p.rectTransform->size[0]:200)+"/"+std::to_string(p.rectTransform?p.rectTransform->size[1]:60);
    const auto u=p.image->texture.generic_u8string(); return "image/"+std::string(u.begin(),u.end());
}
}
namespace SceneRuntime {
std::string SceneUi::Shortcut(const SceneLayout& layout,const std::string& key,unsigned int width,unsigned int height,const UiState& state)
{
    if(key.empty()) return {};
    for(const auto& object:layout.objects)
        if(object.rectTransform && object.button && object.button->enabled && (key.starts_with("action:") ? object.button->inputAction==key.substr(7) : object.button->shortcut==key) &&
            Resolve(layout,object,width,height,state).visible) return object.id;
    return {};
}
void SceneUi::PreparePart(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,
    const ScenePlacement& p,bool text,std::map<std::string,Resource>& pending)
{
    const auto key=p.id+(text?"/text":"/image"),signature=Signature(p,text);
    const auto old=resources_.find(key);
    if(old!=resources_.end() && old->second.signature==signature) return;
    std::shared_ptr<Engine::SpriteRenderer> shared,sharedScene;
    const auto find=[&](const auto& map) {
        for(const auto& [id,resource]:map) {
            static_cast<void>(id);
            if(resource.signature==signature) {shared=resource.sprite; sharedScene=resource.sceneSprite; break;}
        }
    };
    find(resources_); find(pending);
    if(shared) {pending.emplace(key,Resource{signature,std::move(shared),std::move(sharedScene)}); return;}
    auto texture=text?TextTexture(renderer,*p.text,p.rectTransform?p.rectTransform->size:std::array<float,2>{200,60}):std::make_shared<Engine::Texture2D>();
    if(!text && !texture->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),p.image->texture.empty()?std::filesystem::path{}:root/p.image->texture))
        throw std::runtime_error("Cannot load UI image: "+signature);
    auto sprite=std::make_shared<Engine::SpriteRenderer>();
    if(!sprite->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),texture,root/"Shaders/Sprite.hlsl"))
        throw std::runtime_error("Cannot create UI renderer");
    auto sceneSprite=std::make_shared<Engine::SpriteRenderer>();
    if(!sceneSprite->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),texture,root/"Shaders/SceneUi.hlsl",true))
        throw std::runtime_error("Cannot create Scene Canvas renderer");
    pending.emplace(key,Resource{signature,std::move(sprite),std::move(sceneSprite)});
}
void SceneUi::Prune(const Engine::DirectX12Renderer& renderer,const SceneLayout& layout)
{
    std::unordered_set<std::string> keys;
    for(const auto& p:layout.objects) {
        if(p.image) keys.insert(p.id+"/image");
        if(p.text) keys.insert(p.id+"/text");
    }
    if(std::none_of(resources_.begin(),resources_.end(),[&](const auto& item){return !keys.contains(item.first);})) return;
    // The upload fence completes earlier rendering on this queue before releasing unused sprites.
    Engine::Texture2D fenceTexture;
    if(!fenceTexture.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),{})) throw std::runtime_error("Cannot synchronize UI resource release");
    std::erase_if(resources_,[&](const auto& item){return !keys.contains(item.first);});
}
bool SceneUi::Prepare(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,
    const SceneLayout& layout,std::string& error)
{
    try {
        std::map<std::string,Resource> pending;
        for(const auto& p:layout.objects) {
            if(p.image) PreparePart(renderer,root,p,false,pending);
            if(p.text) PreparePart(renderer,root,p,true,pending);
        }
        Prune(renderer,layout);
        for(auto& [key,value]:pending) resources_.insert_or_assign(key,std::move(value));
        error.clear(); return true;
    } catch(const std::exception& exception) {error=exception.what(); return false;}
}
void SceneUi::DrawPart(ID3D12GraphicsCommandList* commands,const ScenePlacement& p,const UiRect& rect,
    unsigned int width,unsigned int height,const UiState& state,bool text) const
{
    const auto resource=resources_.find(p.id+(text?"/text":"/image"));
    if(resource==resources_.end()) return;
    Engine::SpriteDrawParameters draw;
    draw.position=rect.position; draw.size=rect.size; draw.rotation=rect.rotation;
    draw.color=text?p.text->color:p.image->color;
    draw.uvRect=text?std::array<float,4>{0,0,1,1}:p.image->uv;
    if(p.button && p.button->enabled) {
        if(state.pressed==p.id) draw.color=p.button->pressedColor;
        else if(state.hovered==p.id) draw.color=p.button->hoverColor;
    }
    draw.color[3]*=rect.opacity;
    resource->second.sprite->Draw(commands,width,height,draw);
}
void SceneUi::Draw(ID3D12GraphicsCommandList* commands,const SceneLayout& layout,
    unsigned int width,unsigned int height,const UiState& state) const
{
    if(!width || !height) return;
    for(const auto& p:layout.objects) {
        if(!p.rectTransform) continue;
        const auto rect=Resolve(layout,p,width,height,state);
        if(!rect.visible) continue;
        if(p.image && p.image->enabled) DrawPart(commands,p,rect,width,height,state,false);
        if(p.text && p.text->enabled) DrawPart(commands,p,rect,width,height,state,true);
    }
}
void SceneUi::DrawScene(ID3D12GraphicsCommandList* commands,const SceneWorld& world,
    const Engine::Camera& camera,const UiState& state) const
{
    const auto& layout=world.Layout();
    for(const auto& p:layout.objects) {
        if(!p.rectTransform) continue;
        const auto* root=SceneCanvas::Root(layout,p);
        const auto matrix=SceneCanvas::Matrix(world,p);
        if(!root || !matrix) continue;
        const auto rect=Resolve(layout,p,static_cast<unsigned int>(root->canvas->referenceSize[0]),
            static_cast<unsigned int>(root->canvas->referenceSize[1]),state);
        if(!rect.visible) continue;
        DirectX::XMFLOAT4X4 projection;
        DirectX::XMStoreFloat4x4(&projection,DirectX::XMLoadFloat4x4(&*matrix)*camera.GetViewMatrix()*camera.GetProjectionMatrix());
        for(const bool text:{false,true}) {
            if(text ? (!p.text || !p.text->enabled) : (!p.image || !p.image->enabled)) continue;
            const auto resource=resources_.find(p.id+(text?"/text":"/image"));
            if(resource==resources_.end()) continue;
            Engine::SpriteDrawParameters draw;
            draw.position=rect.position; draw.size=rect.size; draw.rotation=rect.rotation;
            draw.color=text?p.text->color:p.image->color;
            draw.color[3]*=rect.opacity;
            draw.uvRect=text?std::array<float,4>{0,0,1,1}:p.image->uv;
            std::memcpy(draw.pixelConstants.data(),&projection,sizeof(projection));
            resource->second.sceneSprite->Draw(commands,1,1,draw);
        }
    }
}
}
