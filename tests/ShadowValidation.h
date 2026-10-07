#pragma once
#include "EnvironmentValidation.h"

namespace ShadowValidation
{
    inline void Run(Engine::DirectX12Renderer& renderer)
    {
        using EnvironmentValidation::Require;
        const auto root=std::filesystem::absolute("generated/tests/shadows");
        std::filesystem::create_directories(root/"Assets/Models");
        { std::ofstream file(root/"Assets/Models/white.mtl"); file << "newmtl white\nKd 1 1 1\n"; }
        { std::ofstream file(root/"Assets/Models/receiver.obj");
          file << "mtllib white.mtl\nusemtl white\nv -20 0 -20\nv -20 0 20\nv 20 0 20\nv 20 0 -20\nf 1 2 3 4\n"; }
        { std::ofstream file(root/"Assets/Models/caster.obj");
          file << "mtllib white.mtl\nusemtl white\nv -1 0 -2\nv -1 0 2\nv 1 0 2\nv 1 0 -2\nf 1 2 3 4\n"; }
        SceneRuntime::SceneLayout layout;
        SceneRuntime::ScenePlacement receiver,rig,caster;
        receiver.id="receiver"; receiver.SetModel("Assets/Models/receiver.obj");
        rig.id="rig";
        caster.id="caster"; caster.parentId="rig"; caster.SetModel("Assets/Models/caster.obj"); caster.position={2,2,0};
        layout.objects={receiver,rig,caster};
        SceneRuntime::ScenePlacement savedLight;
        savedLight.id="light"; savedLight.directionalLight.emplace();
        savedLight.directionalLight->shadowsEnabled=true;
        savedLight.directionalLight->shadowDistance=55;
        savedLight.directionalLight->shadowBias=.0002f;
        layout.objects.push_back(savedLight);
        const auto restored=SceneRuntime::SceneLayout::Parse(layout.Serialize());
        Require(restored.objects.back().directionalLight==savedLight.directionalLight,"shadow enable/range/bias survive JSON save and reload");
        SceneRuntime::SceneWorld world;
        std::string error;
        Require(world.Initialize(renderer,root,layout,std::filesystem::absolute("Content/Shaders/Mesh.hlsl"),&error),"shadow GPU fixture initializes");
        Engine::Camera camera;
        Require(camera.SetPosition({0,5,-8}) && camera.SetOrientation({0,-5,8},{0,1,0}),"shadow fixture camera");
        Engine::DirectionalLight light;
        light.direction={-1,-1,0}; light.specularStrength=0; light.shadowsEnabled=false;
        const auto sample=[&] {
            return EnvironmentValidation::Pixel(renderer,[&](auto* commands){world.Draw(commands,camera,light);});
        };
        const auto unshadowed=sample();
        light.shadowsEnabled=true;
        const auto shaded=sample();
        std::ofstream pixels(root/"pixels.log");
        pixels << "ground lit=" << int(unshadowed[0]) << " shadow=" << int(shaded[0]) << std::endl;
        Require(unshadowed[0]>shaded[0]+70 && shaded[0]>35,"geometry occludes direct light but preserves ambient illumination");
        Require(world.SetLocalTransform("caster",{2,2,0},{0,0,0},{-1,1,1}),"mirror shadow caster");
        const auto mirrored=sample();
        Require(std::abs(int(mirrored[0])-int(shaded[0]))<8,"mirrored and two-sided geometry still cast real shadows");
        Require(world.SetLocalTransform("rig",{6,0,0},{0,0,0},{1,1,1}),"move caster through parent");
        const auto moved=sample();
        Require(moved[0]>shaded[0]+70,"parent transform immediately moves the shadow without rebaking");
        Require(world.SetLocalTransform("rig",{0,0,0},{0,0,0},{1,1,1}),"restore shadow caster parent");
        light.direction={1,-1,0};
        Require(sample()[0]>shaded[0]+70,"changing light direction immediately moves the shadow");
        light.direction={0,0,0};
        const auto zero=sample();
        Require(zero[0]>35 && zero[0]<80,"zero light direction is safe and leaves ambient illumination");
        light.direction={-1,-1,0};
        Require(world.SetLocalTransform("receiver",{0,2,0},{0,0,-DirectX::XM_PIDIV2},{1,1,1}) &&
            world.SetLocalTransform("caster",{2,3,0},{0,0,0},{1,1,1}),"make a vertical receiver");
        Require(camera.SetPosition({6,3,-8}) && camera.SetOrientation({-6,-2,8},{0,1,0}),"vertical receiver camera");
        light.shadowsEnabled=false; const auto wallLit=sample();
        light.shadowsEnabled=true; const auto wallShadow=sample();
        pixels << "wall lit=" << int(wallLit[0]) << " shadow=" << int(wallShadow[0]) << std::endl;
        Require(wallLit[0]>wallShadow[0]+60,"shadows fall on vertical geometry as well as ground");
        Require(renderer.WaitForIdle(),"shadow GPU fixture completes before releasing descriptors");
    }
}
