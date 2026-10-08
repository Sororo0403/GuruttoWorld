#pragma once
#include "SkinningValidation.h"
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <Engine/Graphics/Resources/TextureManager.h>
namespace TextureImportValidation {
    using EnvironmentValidation::Require;
    inline void Run(Engine::DirectX12Renderer& renderer) {
        using namespace Engine;
        TextureLevel odd{7,3,std::vector<unsigned char>(7*3*4,255)}; TextureImportSettings settings; settings.mipmaps=true;
        const auto oddLevels=TextureImport::Build(odd,settings);
        Require(oddLevels.size()==3 && oddLevels[1].width==3 && oddLevels[1].height==1 && oddLevels[2].width==1,"odd texture mip chain reaches one pixel");
        settings.maxSize=4; const auto limited=TextureImport::Build(odd,settings);
        Require(limited[0].width==4 && limited[0].height==1,"texture size limit preserves aspect ratio");
        TextureLevel pair{2,1,{0,0,0,255,255,255,255,255}}; settings.maxSize=16384; settings.srgb=true;
        const auto linear=TextureImport::Build(pair,settings); Require(linear.back().pixels[0]>=187 && linear.back().pixels[0]<=188,"sRGB mip filter averages linear light");
        settings.srgb=false; Require(TextureImport::Build(pair,settings).back().pixels[0]==128,"linear data mip filter preserves arithmetic average");
        bool invalid=false; try { settings.compression="bad"; TextureImport::Build(pair,settings); } catch(const std::exception&){invalid=true;}
        Require(invalid,"unknown texture compression rejected"); settings.compression="bc3";
        std::vector<unsigned char> red(8*8*4); for(size_t i=0;i<red.size();i+=4){red[i]=red[i+3]=255;}
        const auto blocks=TextureImport::Build({8,8,red},settings);
        Require(blocks.size()==4 && blocks[0].pixels.size()==64 && blocks.back().pixels.size()==16,"BC3 blocks include small mip tail");
        auto compressed=std::make_shared<Texture2D>(),plain=std::make_shared<Texture2D>();
        Require(compressed->InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),8,8,red,settings),"BC3 mip texture uploads");
        Require(compressed->GetFormat()==DXGI_FORMAT_BC3_UNORM && compressed->GetMipLevels()==4,"BC3 resource exposes full mip chain");
        settings.compression="none"; Require(plain->InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),8,8,red,settings),"RGBA mip texture uploads");
        SpriteRenderer first,second; const auto shader=std::filesystem::absolute("Content/Shaders/Sprite.hlsl");
        Require(first.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),compressed,shader) && second.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),plain,shader),"texture comparison sprites initialize");
        SpriteDrawParameters draw; draw.position={16,8}; draw.size={8,8};
        const auto actual=SkinningValidation::Capture(renderer,[&](auto* commands){first.Draw(commands,64,32,draw);});
        const auto expected=SkinningValidation::Capture(renderer,[&](auto* commands){second.Draw(commands,64,32,draw);});
        Require(actual==expected && actual[8*256+16*4]==255,"BC3 GPU decompression matches reference pixels");
        draw.size={1,1};
        Require(SkinningValidation::Capture(renderer,[&](auto* commands){first.Draw(commands,64,32,draw);})==SkinningValidation::Capture(renderer,[&](auto* commands){second.Draw(commands,64,32,draw);}),"GPU minification samples uploaded BC3 mip tail");
        auto alphaPixels=red; for(size_t i=0;i<64;++i) alphaPixels[i*4+3]=i%2 ? 255 : 0;
        auto alphaBc=std::make_shared<Texture2D>(),alphaRgba=std::make_shared<Texture2D>(); settings.compression="bc3";
        Require(alphaBc->InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),8,8,alphaPixels,settings),"BC3 alpha texture uploads"); settings.compression="none";
        Require(alphaRgba->InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),8,8,alphaPixels,settings),"reference alpha texture uploads");
        SpriteRenderer alphaFirst,alphaSecond;
        Require(alphaFirst.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),alphaBc,shader) && alphaSecond.Initialize(renderer.GetDevice(),renderer.GetCommandQueue(),alphaRgba,shader),"alpha comparison sprites initialize"); draw.size={8,8};
        Require(SkinningValidation::Capture(renderer,[&](auto* commands){alphaFirst.Draw(commands,64,32,draw);})==SkinningValidation::Capture(renderer,[&](auto* commands){alphaSecond.Draw(commands,64,32,draw);}),"BC3 alpha block decoding matches RGBA blending");
        settings.compression="bc3"; Texture2D fallback;
        Require(fallback.InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),7,3,odd.pixels,settings) && fallback.GetFormat()==DXGI_FORMAT_R8G8B8A8_UNORM,"non-block-aligned texture retains RGBA dimensions");
        Texture2D rejected; settings.maxSize=0;
        Require(!rejected.InitializePixels(renderer.GetDevice(),renderer.GetCommandQueue(),8,8,red,settings) && rejected.GetWidth()==0,"invalid texture setting leaves resource uninitialized");
        const auto directory=std::filesystem::absolute("generated/tests/texture-import/"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())); std::filesystem::create_directories(directory);
        const auto image=directory/"red.bmp"; std::array<unsigned char,54> header{}; header[0]='B';header[1]='M';
        const auto word=[&](size_t offset,uint32_t value){for(size_t byte=0;byte<4;++byte)header[offset+byte]=static_cast<unsigned char>(value>>(byte*8));};
        word(2,310);word(10,54);word(14,40);word(18,8);word(22,8);header[26]=1;header[28]=32;word(34,256);
        { std::ofstream output(image,std::ios::binary); output.write(reinterpret_cast<const char*>(header.data()),header.size()); for(size_t i=0;i<64;++i){const std::array<unsigned char,4> pixel{0,0,255,255};output.write(reinterpret_cast<const char*>(pixel.data()),4);} }
        auto metadata=AssetDatabase::Ensure(image); TextureManager manager; Require(manager.Initialize(renderer.GetDevice(),renderer.GetCommandQueue()),"texture cache initializes");
        const auto initial=manager.Load(image); Require(initial && manager.Load(image)==initial && initial->GetMipLevels()==1,"texture cache reuses unchanged import");
        metadata.texture={true,true,4,"bc3"}; AssetDatabase::Write(image,metadata);
        const auto imported=manager.Load(image); Require(imported && imported!=initial && imported->GetWidth()==4 && imported->GetMipLevels()==3 && imported->GetFormat()==DXGI_FORMAT_BC3_UNORM && initial->GetWidth()==8,"metadata reimport replaces cache while retaining old resource");
        const auto saved=AssetDatabase::Read(image); Require(saved.id==metadata.id && saved.texture.srgb && saved.texture.mipmaps && saved.texture.maxSize==4 && saved.texture.compression=="bc3","texture metadata roundtrips with stable ID");
        metadata.texture.maxSize=0; invalid=false;try{AssetDatabase::Write(image,metadata);}catch(const std::exception&){invalid=true;}
        Require(invalid && AssetDatabase::Read(image).texture.maxSize==4,"invalid import setting preserves saved metadata");
    }
}
