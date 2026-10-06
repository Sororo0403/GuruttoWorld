#pragma once
#include "EnvironmentValidation.h"
#include <SceneRuntime/SceneUi.h>

namespace TitlePresentationValidation
{
    inline void Capture(Engine::DirectX12Renderer& renderer, SceneRuntime::SceneEnvironment& scene,
        unsigned int width,unsigned int height,const std::string& name)
    {
        using EnvironmentValidation::Require;
        Engine::RenderTexture target;
        Require(target.Resize(renderer,width,height),"title capture target");
        const UINT pitch=(width*4+255)&~255u;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC description{};
        description.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width=static_cast<UINT64>(pitch)*height; description.Height=1;
        description.DepthOrArraySize=1; description.MipLevels=1; description.SampleDesc.Count=1;
        description.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        Require(SUCCEEDED(renderer.GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"title capture readback");
        Require(renderer.Render({0,0,0,1},[&](ID3D12GraphicsCommandList* commands,float) {
            Require(target.Begin(commands,{0,0,0,1}),"title capture begins");
            scene.Draw(commands,width,height);
            Require(target.End(commands),"title capture ends");
            D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource=target.GetResource();
            barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
            commands->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION source{},destination{};
            source.pResource=target.GetResource(); source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.pResource=readback.Get(); destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint.Footprint={Engine::RenderTexture::Format,width,height,1,pitch};
            commands->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
            commands->ResourceBarrier(1,&barrier);
        })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"title capture GPU complete");
        unsigned char* pixels=nullptr;
        const D3D12_RANGE range{0,static_cast<SIZE_T>(pitch)*height};
        Require(SUCCEEDED(readback->Map(0,&range,reinterpret_cast<void**>(&pixels))),"title capture maps");
        if (name.starts_with("covered"))
        {
            for (const auto offset:std::array<SIZE_T,4>{0,(width-1)*4,(height-1)*pitch,(height-1)*pitch+(width-1)*4})
                Require(pixels[offset]<20 && pixels[offset+1]<35 && pixels[offset+2]<45 && pixels[offset+3]==255,
                    "start cover reaches all four corners at every aspect ratio");
        }
        const auto directory=std::filesystem::absolute("generated/title-rebuild/previews");
        std::filesystem::create_directories(directory);
        std::ofstream output(directory/(name+".ppm"),std::ios::binary);
        output<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for (UINT y=0;y<height;++y)
            for (UINT x=0;x<width;++x) output.write(reinterpret_cast<char*>(pixels+y*pitch+x*4),3);
        const D3D12_RANGE empty{0,0}; readback->Unmap(0,&empty);
        Require(static_cast<bool>(output),"title capture saved");
    }
    inline void Run(Engine::DirectX12Renderer& renderer,const std::filesystem::path& root)
    {
        using EnvironmentValidation::Require;
        const auto layout=SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/TitleStreet.json");
        SceneRuntime::SceneEnvironment scene; std::string error;
        Require(scene.Initialize(renderer,root,layout,error),"title presentation initializes");
        scene.SeekAnimation(3,3);
        const auto firstCamera=scene.CameraPosition();
        scene.SeekAnimation(8,8);
        Require(scene.CameraPosition()!=firstCamera,"authored waiting motion changes camera pose");
        const auto stopped=scene.CameraPosition();
        scene.Update(0.1,false,true);
        Require(scene.CameraPosition()==stopped,"background OFF freezes waiting animation and sway");
        scene.SeekAnimation(3,3);
        const auto prompt=std::find_if(layout.objects.begin(),layout.objects.end(),[](const auto& o){return o.id=="world-start";});
        Require(prompt!=layout.objects.end(),"title has an editable start prompt");
        for (const auto& size:std::array<std::array<UINT,2>,3>{{{1280,720},{1024,768},{720,1280}}})
        {
            const auto rect=SceneRuntime::SceneUi::Resolve(layout,*prompt,size[0],size[1],scene.Ui());
            Require(rect.visible && rect.position[0]>=0 && rect.position[1]>=0 &&
                rect.position[0]+rect.size[0]<=size[0] && rect.position[1]+rect.size[1]<=size[1],"start prompt fits every window aspect");
            Capture(renderer,scene,size[0],size[1],"idle-"+std::to_string(size[0])+"x"+std::to_string(size[1]));
        }
        scene.SeekAnimation(0.85f,0.85f);
        Capture(renderer,scene,1280,720,"intro");
        scene.SeekAnimation(3,3,0.35f);
        scene.Ui().values["transition"]=0.4375f;
        scene.Ui().values["transitionPink"]=0.546875f;
        Capture(renderer,scene,1280,720,"start");
        scene.SeekAnimation(3,3,0.8f);
        for (const auto& size:std::array<std::array<UINT,2>,3>{{{1280,720},{1024,768},{720,1280}}})
            Capture(renderer,scene,size[0],size[1],"covered-"+std::to_string(size[0])+"x"+std::to_string(size[1]));
        const auto pixel=EnvironmentValidation::Pixel(renderer,[&](ID3D12GraphicsCommandList* commands){scene.Draw(commands,64,32);});
        Require(pixel[0]<20 && pixel[1]<35 && pixel[2]<45,"start transition ends with opaque ink");
        scene.SeekAnimation(3,3,-1);
        Require(scene.Ui().Value("transition")==0 && scene.Ui().Value("transitionPink")==0,"rewinding start restores uncovered title");
        if (GetEnvironmentVariableW(L"WP1_TITLE_PREVIEW",nullptr,0))
        {
            for (int frame=0;frame<64;++frame)
            {
                const float seconds=frame*0.05f;
                scene.SeekAnimation(seconds,seconds);
                Capture(renderer,scene,960,540,"frame-"+std::to_string(frame));
            }
            for (int frame=0;frame<17;++frame)
            {
                scene.SeekAnimation(3.2f,3.2f,frame*0.05f);
                Capture(renderer,scene,960,540,"frame-"+std::to_string(frame+64));
            }
        }
        Require(SceneRuntime::SceneLayout::Load(root/"Assets/Scenes/TitleStreet.json").Serialize()==layout.Serialize(),"preview preserves saved title");
    }
}
