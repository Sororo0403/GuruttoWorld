#include <Engine/Core/Profiler.h>
#include <Engine/Graphics/ShaderCompiler.h>
#include <SceneRuntime/SceneUi.h>
#include <SceneRuntime/SceneCanvas.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <winrt/base.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <stdexcept>
#pragma comment(lib,"gdi32.lib")
namespace {
std::array<UINT,2> TextDimensions(const std::array<float,2>& size) {
    return {static_cast<UINT>(std::clamp(size[0],1.0f,4096.0f)),static_cast<UINT>(std::clamp(size[1],1.0f,4096.0f))};
}
std::size_t TextBytes(const SceneRuntime::ScenePlacement& p) {
    const auto size=TextDimensions(p.rectTransform?p.rectTransform->size:std::array<float,2>{200,60});
    // 行・配置境界を含めて見積もり、小さな文字画像でもGPU割当分を上限へ算入します。
    const auto bytes=((std::size_t(size[0])*4+255)&~std::size_t(255))*size[1];
    return (bytes+65535)&~std::size_t(65535);
}
std::shared_ptr<Engine::Texture2D> TextTexture(const Engine::DirectX12Renderer& renderer,const SceneRuntime::TextComponent& text,const std::array<float,2>& size) {
    Engine::CpuScope rasterScope("UI text raster and upload");
    const auto dimensions=TextDimensions(size);
    const UINT width=dimensions[0],height=dimensions[1];
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
    if(!texture->InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),width,height,std::move(pixels),{},true)) throw std::runtime_error("Cannot upload UI text");
    return texture;
}
std::string Signature(const SceneRuntime::ScenePlacement& p,bool text) {
    if(text) {
        const auto size=TextDimensions(p.rectTransform?p.rectTransform->size:std::array<float,2>{200,60});
        // 長さを付けて本文・フォント名中の改行や区切り文字による衝突を避けます。
        return "text/"+std::to_string(p.text->text.size())+":"+p.text->text+
            std::to_string(p.text->font.size())+":"+p.text->font+"/"+
            std::to_string(static_cast<int>(p.text->fontSize))+"/"+std::to_string(size[0])+"/"+std::to_string(size[1]);
    }
    const auto u=p.image->texture.generic_u8string(); return "image/"+std::string(u.begin(),u.end());
}
}
namespace SceneRuntime {
namespace {
ScenePlacement ControlVisual(const ScenePlacement& source,const UiState& state) {
    auto p=source;
    if((p.slider && p.slider->enabled) || (p.toggle && p.toggle->enabled) || (p.inputField && p.inputField->enabled)) {
        if(!p.image) {p.image.emplace(); p.image->color={.16f,.18f,.22f,1};}
    }
    if(p.inputField && p.inputField->enabled) {
        if(!p.text) {p.text.emplace(); p.text->fontSize=24;}
        p.text->text=SceneUi::InputText(p,state);
        if(state.focused==p.id) p.image->color={.2f,.25f,.35f,1};
    }
    if(p.toggle && p.toggle->enabled) {
        const auto& c=*p.toggle;
        if(state.Value(c.binding.empty()?p.id:c.binding,c.value?1.0f:0.0f)!=0) p.image->color=c.checkedColor;
    }
    return p;
}
void SetClip(Engine::SpriteDrawParameters& draw,const UiRect& rect) {
    std::copy(rect.clip.begin(),rect.clip.end(),draw.pixelConstants.begin()+16);
}
UiRect SliderFill(const ScenePlacement& p,const UiRect& rect,const UiState& state) {
    auto filled=rect; const auto& c=*p.slider;
    const float ratio=std::clamp((state.Value(c.binding.empty()?p.id:c.binding,c.value)-c.minimum)/(c.maximum-c.minimum),0.0f,1.0f);
    const size_t axis=c.vertical?1:0; filled.size[axis]*=ratio;
    const float delta=(filled.size[axis]-rect.size[axis])*.5f;
    filled.position[0]+=(axis==0?std::cos(rect.rotation):-std::sin(rect.rotation))*delta-delta*(axis==0?1:0);
    filled.position[1]+=(axis==0?std::sin(rect.rotation):std::cos(rect.rotation))*delta-delta*(axis==1?1:0);
    return filled;
}
}
std::string SceneUi::Shortcut(const SceneLayout& layout,const std::string& key,unsigned int width,unsigned int height,const UiState& state)
{
    if(key.empty()) return {};
    for(const auto& object:layout.objects)
        if(object.rectTransform && object.button && object.button->enabled && (key.starts_with("action:") ? object.button->inputAction==key.substr(7) : object.button->shortcut==key) &&
            Resolve(layout,object,width,height,state).visible) return object.id;
    return {};
}
void SceneUi::PreparePart(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,
    const ScenePlacement& p,bool text,std::size_t generation,std::map<std::string,Resource>& pending)
{
    const auto key=p.id+(text?"/text":"/image"),signature=Signature(p,text);
    const auto old=resources_.find(key);
    if(old!=resources_.end() && old->second.generation==generation && old->second.signature==signature) { pending.emplace(key,old->second); return; }
    if(text) {
        const auto cached=textCache_.find(signature);
        if(cached!=textCache_.end() && cached->second.resource.generation==generation) {
            Engine::CpuProfiler::Add("UI text cache hit",0);
            pending.emplace(key,cached->second.resource); return;
        }
    }
    std::shared_ptr<Engine::SpriteRenderer> shared,sharedScene;
    const auto find=[&](const auto& map) {
        for(const auto& [id,resource]:map) {
            static_cast<void>(id);
            if(resource.generation==generation && resource.signature==signature) {shared=resource.sprite; sharedScene=resource.sceneSprite; break;}
        }
    };
    find(resources_); find(pending);
    if(shared) {
        pending.emplace(key,Resource{generation,signature,std::move(shared),std::move(sharedScene),text?TextBytes(p):0}); return;
    }
    Engine::CpuScope rebuildScope("UI resource rebuild");
    auto texture=text?TextTexture(renderer,*p.text,p.rectTransform?p.rectTransform->size:std::array<float,2>{200,60}):std::make_shared<Engine::Texture2D>();
    if(!text && !texture->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),p.image->texture.empty()?std::filesystem::path{}:root/p.image->texture))
        throw std::runtime_error("Cannot load UI image: "+signature);
    std::shared_ptr<Engine::SpriteRenderer> prototype,scenePrototype;
    const auto findPrototype=[&](const auto& map) {
        for(const auto& [id,resource]:map) {
            static_cast<void>(id);
            if(resource.generation==generation) { prototype=resource.sprite; scenePrototype=resource.sceneSprite; break; }
        }
    };
    findPrototype(resources_); if(!prototype) findPrototype(pending);
    auto sprite=std::make_shared<Engine::SpriteRenderer>();
    if(!(prototype?sprite->InitializeShared(*prototype,texture):
        sprite->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),texture,root/"Shaders/Ui.hlsl")))
        throw std::runtime_error("Cannot create UI renderer");
    auto sceneSprite=std::make_shared<Engine::SpriteRenderer>();
    if(!(scenePrototype?sceneSprite->InitializeShared(*scenePrototype,texture):
        sceneSprite->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),texture,root/"Shaders/SceneUi.hlsl",true)))
        throw std::runtime_error("Cannot create Scene Canvas renderer");
    pending.emplace(key,Resource{generation,signature,std::move(sprite),std::move(sceneSprite),text?TextBytes(p):0});
}
bool SceneUi::Prepare(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,
    const SceneLayout& layout,std::string& error,const UiState& state,bool inspectShaders)
{
    try {
        if(resources_.empty() && std::none_of(layout.objects.begin(),layout.objects.end(),[](const auto& p) {
            return p.image || p.text || p.slider || p.toggle || p.inputField;
        })) { error.clear(); return true; }
        auto retirement=Engine::GpuRetirement::Shared(renderer.GetDevice(),renderer.GetCommandQueue());
        if(!retirement) throw std::runtime_error("Cannot create UI retirement fence");
        retirement->Collect();
        const bool contextChanged=preparedDevice_!=renderer.GetDevice() || preparedQueue_!=renderer.GetCommandQueue() || preparedRoot_!=root;
        const bool inspected=inspectShaders || contextChanged || contextGeneration_==0;
        std::string uiRevision,sceneRevision;
        auto preparedRoot=root;
        auto generation=contextGeneration_;
        if(inspected) {
            if(!Engine::GetShaderRevision(root/"Shaders/Ui.hlsl",uiRevision) ||
                !Engine::GetShaderRevision(root/"Shaders/SceneUi.hlsl",sceneRevision))
                throw std::runtime_error("Cannot inspect UI shaders");
            if(contextChanged || generation==0 || uiRevision!=uiRevision_ || sceneRevision!=sceneRevision_) ++generation;
        }
        std::map<std::string,Resource> pending;
        for(const auto& source:layout.objects) {
            if(!source.image && !source.text && !source.slider && !source.toggle && !source.inputField) continue;
            const auto p=ControlVisual(source,state);
            if(p.image) PreparePart(renderer,root,p,false,generation,pending);
            if(p.text) PreparePart(renderer,root,p,true,generation,pending);
        }
        // キャッシュも候補として作り、資源準備・GPU退役が失敗した場合は旧表示と履歴を保持します。
        constexpr std::size_t TextCacheLimit=32,TextCacheBytes=16*1024*1024;
        const bool cacheChanged=generation!=contextGeneration_ || std::any_of(pending.begin(),pending.end(),[&](const auto& item) {
            const auto old=resources_.find(item.first);
            return item.second.textBytes && (old==resources_.end() || old->second.sprite!=item.second.sprite);
        });
        std::map<std::string,CachedText> cache;
        auto cacheClock=textCacheClock_;
        if(cacheChanged) cache=textCache_;
        for(auto it=cache.begin();it!=cache.end();) {
            if(it->second.resource.generation!=generation) it=cache.erase(it); else ++it;
        }
        for(const auto& [key,resource]:pending) {
            static_cast<void>(key);
            if(cacheChanged && resource.textBytes && resource.textBytes<=TextCacheBytes)
                cache.insert_or_assign(resource.signature,CachedText{resource,++cacheClock});
        }
        std::size_t cacheBytes=0;
        for(const auto& [key,cached]:cache) {static_cast<void>(key); cacheBytes+=cached.resource.textBytes;}
        while(cache.size()>TextCacheLimit || cacheBytes>TextCacheBytes) {
            const auto oldest=std::min_element(cache.begin(),cache.end(),[](const auto& a,const auto& b) {return a.second.lastUse<b.second.lastUse;});
            cacheBytes-=oldest->second.resource.textBytes; cache.erase(oldest);
        }
        auto replaced=std::make_shared<std::map<std::string,Resource>>();
        if(cacheChanged) for(const auto& [key,cached]:textCache_) {
            const auto replacement=cache.find(key);
            if(replacement==cache.end() || replacement->second.resource.sprite!=cached.resource.sprite)
                replaced->emplace("cache/"+key,cached.resource);
        }
        for(const auto& [key,value]:resources_) {
            const auto replacement=pending.find(key);
            if(replacement==pending.end() || replacement->second.sprite!=value.sprite ||
                replacement->second.sceneSprite!=value.sceneSprite) replaced->emplace(key,value);
        }
        // Register the old resources before committing. A failed signal leaves the current UI intact.
        if(!replaced->empty() && !(retirement_?retirement_:retirement)->Retire(replaced)) throw std::runtime_error("Cannot retire UI resources");
        retirement_=std::move(retirement);
        resources_.swap(pending);
        if(cacheChanged) {textCache_.swap(cache); textCacheClock_=cacheClock;}
        preparedRoot_.swap(preparedRoot);
        preparedDevice_=renderer.GetDevice(); preparedQueue_=renderer.GetCommandQueue();
        contextGeneration_=generation;
        if(inspected) { uiRevision_.swap(uiRevision); sceneRevision_.swap(sceneRevision); }
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
    SetClip(draw,rect);
    resource->second.sprite->Draw(commands,width,height,draw);
}
void SceneUi::Draw(ID3D12GraphicsCommandList* commands,const SceneLayout& layout,
    unsigned int width,unsigned int height,const UiState& state) const
{
    if(!width || !height) return;
    for(const auto& source:layout.objects) {
        if(!source.rectTransform) continue;
        const auto p=ControlVisual(source,state);
        const auto rect=Resolve(layout,source,width,height,state);
        if(!rect.visible) continue;
        if(p.image && p.image->enabled) DrawPart(commands,p,rect,width,height,state,false);
        if(p.slider && p.slider->enabled && p.image) {
            auto fill=p; fill.image->color=p.slider->fillColor;
            DrawPart(commands,fill,SliderFill(p,rect,state),width,height,state,false);
        }
        if(p.text && p.text->enabled) DrawPart(commands,p,rect,width,height,state,true);
    }
}
void SceneUi::DrawScene(ID3D12GraphicsCommandList* commands,const SceneWorld& world,
    const Engine::Camera& camera,const UiState& state) const
{
    const auto& layout=world.Layout();
    for(const auto& source:layout.objects) {
        if(!source.rectTransform) continue;
        const auto p=ControlVisual(source,state);
        const auto* root=SceneCanvas::Root(layout,p);
        const auto matrix=SceneCanvas::Matrix(world,p);
        if(!root || !matrix) continue;
        const auto rect=Resolve(layout,source,static_cast<unsigned int>(root->canvas->referenceSize[0]),
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
            SetClip(draw,rect);
            resource->second.sceneSprite->Draw(commands,1,1,draw);
            if(!text && p.slider && p.slider->enabled) {
                const auto filled=SliderFill(p,rect,state);
                draw.position=filled.position; draw.size=filled.size; draw.color=p.slider->fillColor; draw.color[3]*=rect.opacity;
                resource->second.sceneSprite->Draw(commands,1,1,draw);
            }
        }
    }
}
}
