#pragma once
#include <SceneRuntime/SceneView.h>
#include <Engine/Graphics/Models/ModelLoader.h>
#include <fstream>
#include <cstring>

namespace SceneRuntime
{
    class LightmapBaker final
    {
    public:
        /// <summary>元UV上へ静的な平行・局所光源と実形状の遮蔽をCPUでベイクします。</summary>
        static std::vector<unsigned char> Bake(const SceneWorld& world,const std::filesystem::path& root,
            const std::string& id,UINT resolution=128)
        {
            if (resolution<16 || resolution>512) throw std::runtime_error("Lightmap resolution must be within [16,512]");
            const auto& objects=world.Layout().objects;
            const auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& object) { return object.id==id; });
            if (found==objects.end() || !found->meshRenderer || found->animator) throw std::runtime_error("Lightmap baking needs a static MeshRenderer");
            std::vector<Engine::MeshData> meshes;
            if (!Engine::ModelLoader::Load(root/found->Model(),meshes)) throw std::runtime_error("Cannot load lightmap source geometry");
            DirectX::XMFLOAT4X4 transform;
            if (!world.WorldMatrix(id,transform)) throw std::runtime_error("Cannot resolve lightmap transform");
            const auto lighting=SceneView::Light(world);
            std::vector<unsigned char> pixels(static_cast<size_t>(resolution)*resolution*4,0);
            bool covered=false;
            for (const auto& mesh : meshes)
                for (size_t index=0;index+2<mesh.indices.size();index+=3)
                {
                    const std::array<Engine::MeshVertex,3> triangle{mesh.vertices.at(mesh.indices[index]),mesh.vertices.at(mesh.indices[index+1]),mesh.vertices.at(mesh.indices[index+2])};
                    covered=Rasterize(world,triangle,transform,lighting,resolution,pixels) || covered;
                }
            if (!covered) throw std::runtime_error("Mesh needs nondegenerate UVs in [0,1] for baking");
            Dilate(resolution,pixels); return pixels;
        }
        /// <summary>線形照度をsRGBの32bit BMPとして安全なAssets/Texturesパスへ保存します。</summary>
        static void Save(const std::filesystem::path& root,const std::filesystem::path& path,UINT resolution,
            const std::vector<unsigned char>& pixels)
        {
            const auto text=path.generic_string();
            if (path.is_absolute() || path.has_root_name() || !text.starts_with("Assets/Textures/") || path.extension()!=".bmp" ||
                std::any_of(path.begin(),path.end(),[](const auto& part) { return part==".."; }) || resolution<16 || resolution>512 ||
                pixels.size()!=static_cast<size_t>(resolution)*resolution*4)
                throw std::runtime_error("Invalid lightmap output");
            BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
            file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
            BITMAPINFOHEADER image{}; image.biSize=sizeof(image); image.biWidth=static_cast<LONG>(resolution);
            image.biHeight=-static_cast<LONG>(resolution); image.biPlanes=1; image.biBitCount=32; image.biCompression=BI_RGB;
            auto bgra=pixels; for (size_t index=0;index<bgra.size();index+=4) std::swap(bgra[index],bgra[index+2]);
            const auto base=std::filesystem::weakly_canonical(root),target=std::filesystem::weakly_canonical(root/path);
            const auto relative=target.lexically_relative(base);
            if (relative.empty() || *relative.begin()=="..") throw std::runtime_error("Lightmap output leaves project root");
            std::filesystem::create_directories(target.parent_path());
            auto temporary=target; temporary+=".tmp";
            std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
            output.write(reinterpret_cast<const char*>(&file),sizeof(file)); output.write(reinterpret_cast<const char*>(&image),sizeof(image));
            output.write(reinterpret_cast<const char*>(bgra.data()),static_cast<std::streamsize>(bgra.size())); output.close();
            if (!output || !MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot save baked lightmap");
        }
    private:
        /// <summary>光源へ向かう実形状のレイで照明の可視性を確認します。</summary>
        static bool Visible(const SceneWorld& world,const DirectX::XMFLOAT3& position,const DirectX::XMFLOAT3& normal,
            const DirectX::XMFLOAT3& direction,float distance)
        {
            return !world.PickRay({position.x+normal.x*.002f,position.y+normal.y*.002f,position.z+normal.z*.002f},
                {direction.x,direction.y,direction.z},distance);
        }
        /// <summary>環境光と遮蔽された平行・点・スポット光の照度を評価します。</summary>
        static std::array<float,3> Irradiance(const SceneWorld& world,const DirectX::XMFLOAT3& position,
            const DirectX::XMFLOAT3& normal,const Engine::DirectionalLight& lighting)
        {
            using namespace DirectX;
            std::array<float,3> result{lighting.ambientIntensity,lighting.ambientIntensity,lighting.ambientIntensity};
            const auto n=XMLoadFloat3(&normal),p=XMLoadFloat3(&position);
            const auto contribution=[&](FXMVECTOR direction,float distance,const std::array<float,3>& color,float intensity) {
                const float cosine=(std::max)(0.0f,XMVectorGetX(XMVector3Dot(n,direction)));
                XMFLOAT3 ray; XMStoreFloat3(&ray,direction);
                if (cosine<=0 || intensity<=0 || !Visible(world,position,normal,ray,distance)) return;
                for (size_t channel=0;channel<3;++channel) result[channel]+=color[channel]*intensity*cosine;
            };
            const auto direct=XMVectorSet(-lighting.direction[0],-lighting.direction[1],-lighting.direction[2],0);
            if (XMVectorGetX(XMVector3LengthSq(direct))>1e-8f) contribution(XMVector3Normalize(direct),100000,lighting.color,lighting.intensity);
            for (const auto& light : lighting.localLights)
            {
                const auto difference=XMVectorSet(light.position[0],light.position[1],light.position[2],1)-p;
                const float squared=XMVectorGetX(XMVector3LengthSq(difference)),distance=std::sqrt(squared);
                if (distance<=.0001f || distance>=light.range) continue;
                const auto direction=difference/distance;
                const float normalized=squared/(light.range*light.range),window=1-normalized*normalized;
                float intensity=light.intensity*window*window/(std::max)(squared,.0001f);
                if (light.spot)
                {
                    const auto forward=XMVector3Normalize(XMVectorSet(light.direction[0],light.direction[1],light.direction[2],0));
                    const float cone=std::clamp((XMVectorGetX(XMVector3Dot(forward,-direction))-light.outerCosine)/(light.innerCosine-light.outerCosine),0.0f,1.0f);
                    intensity*=cone*cone;
                }
                contribution(direction,(std::max)(distance-.002f,.0001f),light.color,intensity);
            }
            return result;
        }
        /// <summary>UVの三角形を画素へ走査し、補間したワールド位置と法線を照明へ渡します。</summary>
        static bool Rasterize(const SceneWorld& world,const std::array<Engine::MeshVertex,3>& triangle,const DirectX::XMFLOAT4X4& transform,
            const Engine::DirectionalLight& lighting,UINT resolution,std::vector<unsigned char>& pixels)
        {
            const auto& a=triangle[0].uv; const auto& b=triangle[1].uv; const auto& c=triangle[2].uv;
            for (const auto& vertex : triangle)
                for (const float uv : vertex.uv)
                    if (!std::isfinite(uv) || uv<0 || uv>1) throw std::runtime_error("Lightmap UVs must be finite and within [0,1]");
            const float determinant=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
            if (std::abs(determinant)<1e-10f) return false;
            const auto matrix=DirectX::XMLoadFloat4x4(&transform),normalMatrix=DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr,matrix));
            bool covered=false;
            const auto pixel=[&](float uv) { return static_cast<UINT>(std::clamp(uv*resolution,0.0f,static_cast<float>(resolution-1))); };
            const UINT left=pixel((std::min)({a[0],b[0],c[0]})),right=pixel((std::max)({a[0],b[0],c[0]}));
            const UINT top=pixel((std::min)({a[1],b[1],c[1]})),bottom=pixel((std::max)({a[1],b[1],c[1]}));
            for (UINT y=top;y<=bottom;++y) for (UINT x=left;x<=right;++x)
            {
                const float u=(x+.5f)/resolution-a[0],v=(y+.5f)/resolution-a[1];
                const float wb=(u*(c[1]-a[1])-v*(c[0]-a[0]))/determinant,wc=((b[0]-a[0])*v-(b[1]-a[1])*u)/determinant,wa=1-wb-wc;
                if (wa<0 || wb<0 || wc<0) continue;
                const auto color=Shade(world,triangle,matrix,normalMatrix,lighting,{wa,wb,wc});
                const size_t offset=(static_cast<size_t>(y)*resolution+x)*4;
                for (size_t channel=0;channel<3;++channel)
                {
                    const float linear=std::clamp(color[channel],0.0f,1.0f),encoded=linear<=.0031308f ? linear*12.92f : 1.055f*std::pow(linear,1/2.4f)-.055f;
                    pixels[offset+channel]=static_cast<unsigned char>(std::lround(encoded*255));
                }
                pixels[offset+3]=255; covered=true;
            }
            return covered;
        }
        /// <summary>重心座標の位置・法線をワールドへ変換し、一つのUV画素の照度を評価します。</summary>
        static std::array<float,3> Shade(const SceneWorld& world,const std::array<Engine::MeshVertex,3>& triangle,
            const DirectX::XMMATRIX& matrix,const DirectX::XMMATRIX& normalMatrix,const Engine::DirectionalLight& lighting,const std::array<float,3>& weights)
        {
            std::array<float,3> local{},normal{};
            for (size_t axis=0;axis<3;++axis) for (size_t vertex=0;vertex<3;++vertex)
            { local[axis]+=triangle[vertex].position[axis]*weights[vertex]; normal[axis]+=triangle[vertex].normal[axis]*weights[vertex]; }
            DirectX::XMFLOAT3 position,n;
            DirectX::XMStoreFloat3(&position,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(local[0],local[1],local[2],1),matrix));
            DirectX::XMStoreFloat3(&n,DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(normal[0],normal[1],normal[2],0),normalMatrix)));
            if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) throw std::runtime_error("Lightmap source has degenerate normals");
            return Irradiance(world,position,n,lighting);
        }
        /// <summary>未使用UV境界へ隣接色を広げ、補間時の黒い継ぎ目を減らします。</summary>
        static void Dilate(UINT resolution,std::vector<unsigned char>& pixels)
        {
            for (size_t pass=0;pass<2;++pass)
            {
                auto source=pixels;
                for (UINT y=0;y<resolution;++y) for (UINT x=0;x<resolution;++x)
                {
                    const size_t target=(static_cast<size_t>(y)*resolution+x)*4; if (source[target+3]) continue;
                    for (const auto& offset : std::array<std::array<int,2>,4>{{{-1,0},{1,0},{0,-1},{0,1}}})
                    {
                        const int nx=static_cast<int>(x)+offset[0],ny=static_cast<int>(y)+offset[1];
                        if (nx<0 || ny<0 || nx>=static_cast<int>(resolution) || ny>=static_cast<int>(resolution)) continue;
                        const size_t neighbor=(static_cast<size_t>(ny)*resolution+nx)*4;
                        if (source[neighbor+3]) { std::copy_n(source.begin()+neighbor,4,pixels.begin()+target); break; }
                    }
                }
            }
        }
    };
}
