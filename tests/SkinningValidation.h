#pragma once
#include "EnvironmentValidation.h"
#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <Engine/Animation/Skeleton.h>

namespace SkinningValidation
{
    using EnvironmentValidation::Require;
    inline std::vector<unsigned char> Capture(Engine::DirectX12Renderer& renderer,
        const std::function<void(ID3D12GraphicsCommandList*)>& draw,Engine::ShadowMap* shadow=nullptr)
    {
        Engine::RenderTexture target;
        Require(target.Resize(renderer,64,32),"skin readback target");
        auto* sourceResource=shadow ? shadow->Resource() : target.GetResource();
        const auto sourceDescription=sourceResource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 bytes=0;
        renderer.GetDevice()->GetCopyableFootprints(&sourceDescription,0,1,0,&footprint,nullptr,nullptr,&bytes);
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width=bytes; buffer.Height=1; buffer.DepthOrArraySize=buffer.MipLevels=1;
        buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        Require(SUCCEEDED(renderer.GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"skin readback buffer");
        Require(renderer.Render({0,0,0,1},[&](auto* commands,float) {
            Require(target.Begin(commands,{0,0,0,1}),"skin target begins"); draw(commands);
            Require(target.End(commands),"skin target ends");
            D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition={sourceResource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
            commands->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION source{},destination{};
            source.pResource=sourceResource; source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.pResource=readback.Get(); destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint=footprint; commands->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter); commands->ResourceBarrier(1,&barrier);
        })!=Engine::RenderResult::Failed && renderer.WaitForIdle(),"skin capture completes");
        unsigned char* mapped=nullptr; const D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};
        Require(SUCCEEDED(readback->Map(0,&range,reinterpret_cast<void**>(&mapped))),"skin readback maps");
        std::vector<unsigned char> result(mapped,mapped+bytes); const D3D12_RANGE empty{}; readback->Unmap(0,&empty); return result;
    }
    inline void Rendering(Engine::DirectX12Renderer& renderer)
    {
        using namespace Engine; using namespace DirectX;
        const auto shader=std::filesystem::absolute("Content/Shaders/Mesh.hlsl");
        SkeletonData rig; rig.nodes={{"mesh",-1,{}},{"first",-1,{}},{"second",-1,{}}}; rig.importScale=.7f;
        XMStoreFloat4x4(&rig.inverseRoot,XMMatrixTranslation(.1f,0,0));
        RiggedMesh source; source.node=0; source.joints={1,2}; source.inverseBind.resize(2);
        for (auto& matrix : source.inverseBind) XMStoreFloat4x4(&matrix,XMMatrixIdentity());
        source.mesh.vertices={{{-1,-1,.4f},{.3f,.2f,-1}},{{0,1,.4f},{.3f,.2f,-1}},{{1,-1,.4f},{.3f,.2f,-1}}};
        source.mesh.indices={0,1,2}; source.weights={{{0,1,0,0},{2,1,0,0}},{{0,1,0,0},{1,3,0,0}},{{0,1,0,0},{0,0,0,0}}};
        MeshRenderer gpu; Require(gpu.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),Skeleton::BindMesh(source),shader),"GPU bind geometry initializes");
        auto* geometry=gpu.GeometryResource();
        XMFLOAT4X4 identity; XMStoreFloat4x4(&identity,XMMatrixIdentity());
        DirectionalLight light; light.direction={0,0,1}; light.ambientIntensity=.1f; light.specularStrength=0;
        ShadowMap shadow; Require(shadow.Initialize(renderer.GetDevice(),shader),"skinned shadow initializes");
        Camera camera; camera.SetPosition({0,0,-3}); light.shadowDistance=10;
        std::vector<unsigned char> previous;
        for (int step=0;step<3;++step)
        {
            auto pose=Skeleton::Sample(rig,"",0,false); pose[0].position[0]=.15f*step;
            pose[1].scale={1+.2f*step,1-.1f*step,1}; pose[1].position[0]=-.25f*step;
            pose[2].rotation={0,0,std::sin(.2f*step),std::cos(.2f*step)}; pose[2].position[1]=.1f*step;
            const auto matrices=Skeleton::Matrices(rig,pose); const auto palette=Skeleton::Palette(rig,source,matrices);
            MeshRenderer cpu; Require(cpu.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),Skeleton::Skin(rig,source,matrices),shader),"CPU oracle initializes");
            const auto expected=Capture(renderer,[&](auto* commands) { cpu.Draw(commands,identity,identity,light); });
            const auto actual=Capture(renderer,[&](auto* commands) { gpu.Draw(commands,identity,identity,light,{0,0,-3.5f},{},nullptr,palette); });
            size_t visible=0;
            for (size_t pixel=0;pixel<actual.size();++pixel) { Require(std::abs(int(actual[pixel])-int(expected[pixel]))<=2,"GPU positions and inverse-transpose normals match CPU image"); if (pixel%4==0 && actual[pixel]>0) ++visible; }
            Require(visible>100,"skin fixture produces visible pixels");
            if (!previous.empty()) Require(actual!=previous,"different poses change rasterized geometry"); previous=actual;
            const auto depth=[&](bool skinned) { return Capture(renderer,[&](auto* commands) {
                Require(shadow.Begin(commands,camera,light),"skin shadow begins");
                if (skinned) gpu.DrawShadow(commands,identity,shadow,palette); else cpu.DrawShadow(commands,identity,shadow);
                shadow.End(commands);
            },&shadow); };
            const auto expectedDepth=depth(false),actualDepth=depth(true);
            size_t covered=0;
            for (size_t offset=0;offset<actualDepth.size();offset+=sizeof(float))
            {
                float first,second; std::memcpy(&first,expectedDepth.data()+offset,sizeof(float)); std::memcpy(&second,actualDepth.data()+offset,sizeof(float));
                Require(std::abs(first-second)<.00001f,"GPU shadow depth matches CPU posed mesh"); if (second<1) ++covered;
            }
            Require(covered>100,"posed geometry casts real shadow depth");
            auto later=palette; for (auto& value : later) value.position._41+=4;
            const auto retained=Capture(renderer,[&](auto* commands) {
                gpu.Draw(commands,identity,identity,light,{0,0,-3.5f},{},nullptr,palette);
                gpu.Draw(commands,identity,identity,light,{0,0,-3.5f},{},nullptr,later);
            });
            Require(retained==actual,"different palettes in same frame preserve earlier draw");
            Require(gpu.GeometryResource()==geometry,"pose changes keep immutable GPU vertices");
            for (int frame=0;frame<6;++frame)
                Require(renderer.Render({0,0,0,1},[&](auto* commands,float) { gpu.Draw(commands,identity,identity,light,{0,0,-3.5f},{},nullptr,frame%2 ? palette : later); })!=RenderResult::Failed,"poses support multiple in-flight frames");
            Require(renderer.WaitForIdle(),"skin frame resources finish");
        }
    }
    inline void Model(Engine::DirectX12Renderer& renderer)
    {
        using namespace Engine; using namespace DirectX;
        ModelRenderer model; Require(model.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),std::filesystem::absolute("Content/Assets/Models/AnimatedBox.gltf"),std::filesystem::absolute("Content/Shaders/Mesh.hlsl")),"GPU animated model initializes");
        const auto copy=model.AnimatedCopy(renderer.GetDevice(),renderer.GetCommandQueue());
        Require(copy && copy->GeometryResource(0)==model.GeometryResource(0),"animated instances share immutable geometry");
        auto pose=Skeleton::Sample(*model.Rig(),"Walk",.5,false); auto* geometry=model.GeometryResource(0);
        Require(model.ApplyPose(pose) && model.GeometryResource(0)==geometry,"model pose does not upload new vertices");
        Require(copy->Pose().back().rotation!=model.Pose().back().rotation,"shared geometry retains independent instance pose");
        const auto matrices=Skeleton::Matrices(*model.Rig(),pose);
        bool selected=false;
        for (const auto& source : model.Rig()->meshes)
        {
            const auto baked=Skeleton::Skin(*model.Rig(),source,matrices);
            for (size_t triangle=0;triangle+2<baked.indices.size();triangle+=3)
            {
                XMVECTOR points[3];
                for (size_t index=0;index<3;++index)
                {
                    const auto& p=baked.vertices[baked.indices[triangle+index]].position; points[index]=XMVectorSet(p[0],p[1],p[2],0);
                    Require(model.Bounds().Contains(points[index])!=DISJOINT,"bone group bounds contain every posed vertex");
                }
                const auto center=(points[0]+points[1]+points[2])/3;
                const auto normal=XMVector3Normalize(XMVector3Cross(points[1]-points[0],points[2]-points[0]));
                float hit=0; if (model.IntersectRay(center+normal*2,-normal,hit)) { Require(hit>0 && hit<=2.001f,"lazy picking uses current posed triangles"); selected=true; }
            }
        }
        Require(selected,"posed model selectable");
        std::vector<std::unique_ptr<MeshRenderer>> bakedMeshes;
        for (const auto& source : model.Rig()->meshes)
        {
            auto mesh=std::make_unique<MeshRenderer>();
            Require(mesh->Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),Skeleton::Skin(*model.Rig(),source,matrices),std::filesystem::absolute("Content/Shaders/Mesh.hlsl")),"model CPU comparison initializes");
            bakedMeshes.push_back(std::move(mesh));
        }
        const auto box=model.Bounds(); const float scale=.6f/std::max({box.Extents.x,box.Extents.y,box.Extents.z});
        XMFLOAT4X4 world,projection;
        XMStoreFloat4x4(&world,XMMatrixTranslation(-box.Center.x,-box.Center.y,-box.Center.z)*XMMatrixScaling(scale,scale,scale)*XMMatrixTranslation(0,0,.5f));
        XMStoreFloat4x4(&projection,XMMatrixIdentity()); DirectionalLight light; light.enabled=false;
        const auto expected=Capture(renderer,[&](auto* commands) { for (const auto& mesh : bakedMeshes) mesh->Draw(commands,world,projection,light); });
        const auto actual=Capture(renderer,[&](auto* commands) { model.Draw(commands,world,projection,light); });
        Require(actual==expected,"ModelRenderer forwards actual instance palettes to GPU");
        Require(static_cast<size_t>(std::ranges::count_if(actual,[](unsigned char value) { return value!=0; }))>actual.size()/4+20,"animated model comparison renders visible geometry");
        const auto bounds=model.Bounds(); const auto rotation=model.Pose().back().rotation;
        pose.back().scale[0]=0;
        Require(!model.ApplyPose(pose) && model.Pose().back().rotation==rotation && model.Bounds().Center.x==bounds.Center.x && model.GeometryResource(0)==geometry,"invalid pose preserves geometry and pose");
    }
}
