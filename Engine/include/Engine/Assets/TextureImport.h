#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace Engine {
struct TextureImportSettings {
    bool mipmaps=false,srgb=false;
    unsigned int maxSize=16384;
    std::string compression="none";
    void Validate() const {
        if (!maxSize || maxSize>16384 || (compression!="none" && compression!="bc3")) throw std::runtime_error("Invalid texture import settings");
    }
};
struct TextureLevel { unsigned int width=0,height=0; std::vector<unsigned char> pixels; };
class TextureImport final {
    static TextureLevel Resize(const TextureLevel& input,unsigned int width,unsigned int height,bool srgb) {
        TextureLevel result{width,height,std::vector<unsigned char>(size_t(width)*height*4)};
        for (unsigned int y=0;y<height;++y) for (unsigned int x=0;x<width;++x) {
            const auto x0=x*input.width/width,x1=(x+1)*input.width/width;
            const auto y0=y*input.height/height,y1=(y+1)*input.height/height;
            std::array<double,4> sum{}; double count=0;
            for (auto sy=y0;sy<y1;++sy) for (auto sx=x0;sx<x1;++sx) {
                for (size_t c=0;c<4;++c) {
                    double v=input.pixels[(size_t(sy)*input.width+sx)*4+c]/255.0;
                    if (srgb && c<3) v=v<=.04045 ? v/12.92 : std::pow((v+.055)/1.055,2.4);
                    sum[c]+=v;
                } ++count;
            }
            for (size_t c=0;c<4;++c) {
                double v=sum[c]/count;
                if (srgb && c<3) v=v<=.0031308 ? v*12.92 : 1.055*std::pow(v,1/2.4)-.055;
                result.pixels[(size_t(y)*width+x)*4+c]=static_cast<unsigned char>(std::clamp(std::lround(v*255),0L,255L));
            }
        } return result;
    }
    static uint16_t Pack(const std::array<unsigned char,4>& p) { return uint16_t((unsigned(p[0])>>3)<<11 | (unsigned(p[1])>>2)<<5 | (unsigned(p[2])>>3)); }
    static std::array<int,3> Unpack(uint16_t p) { return {int((p>>11)*255/31),int(((p>>5)&63)*255/63),int((p&31)*255/31)}; }
    static TextureLevel Compress(const TextureLevel& input) {
        TextureLevel result{input.width,input.height,std::vector<unsigned char>(size_t((input.width+3)/4)*((input.height+3)/4)*16)};
        size_t offset=0;
        for (unsigned int y=0;y<input.height;y+=4) for (unsigned int x=0;x<input.width;x+=4) {
            std::array<std::array<unsigned char,4>,16> block{};
            std::array<unsigned char,4> low{255,255,255,255},high{};
            for (size_t i=0;i<16;++i) for (size_t c=0;c<4;++c) {
                const auto sx=std::min(x+unsigned(i%4),input.width-1),sy=std::min(y+unsigned(i/4),input.height-1);
                const auto v=input.pixels[(size_t(sy)*input.width+sx)*4+c]; block[i][c]=v; low[c]=std::min(low[c],v); high[c]=std::max(high[c],v);
            }
            auto* output=result.pixels.data()+offset; offset+=16;
            output[0]=high[3]; output[1]=low[3]; std::array<int,8> alpha{high[3],low[3]};
            if (high[3]>low[3]) for (int i=2;i<8;++i) alpha[size_t(i)]=((8-i)*high[3]+(i-1)*low[3])/7;
            else { for (int i=2;i<6;++i) alpha[size_t(i)]=((6-i)*high[3]+(i-1)*low[3])/5; alpha[6]=0; alpha[7]=255; }
            uint64_t alphaBits=0;
            const uint16_t a=Pack(high),b=Pack(low); output[8]=static_cast<unsigned char>(a); output[9]=static_cast<unsigned char>(a>>8); output[10]=static_cast<unsigned char>(b); output[11]=static_cast<unsigned char>(b>>8);
            std::array<std::array<int,3>,4> colors{Unpack(a),Unpack(b)};
            for (size_t c=0;c<3;++c) { colors[2][c]=(2*colors[0][c]+colors[1][c])/3; colors[3][c]=(colors[0][c]+2*colors[1][c])/3; }
            uint32_t colorBits=0;
            for (size_t i=0;i<16;++i) {
                unsigned int ai=0,ci=0; int ad=1000,cd=1000000;
                for (unsigned int j=0;j<8;++j) { const int d=std::abs(int(block[i][3])-alpha[j]); if(d<ad){ad=d;ai=j;} }
                for (unsigned int j=0;j<4;++j) { int d=0; for(size_t c=0;c<3;++c){const int v=int(block[i][c])-colors[j][c];d+=v*v;} if(d<cd){cd=d;ci=j;} }
                alphaBits|=uint64_t(ai)<<(i*3); colorBits|=ci<<(i*2);
            }
            for(size_t i=0;i<6;++i) output[i+2]=static_cast<unsigned char>(alphaBits>>(i*8));
            for(size_t i=0;i<4;++i) output[i+12]=static_cast<unsigned char>(colorBits>>(i*8));
        } return result;
    }
public:
    static bool Compressed(const TextureImportSettings& settings,const TextureLevel& source) { return settings.compression=="bc3" && source.width%4==0 && source.height%4==0; }
    static std::vector<TextureLevel> Build(TextureLevel source,const TextureImportSettings& settings) {
        settings.Validate();
        if (!source.width || !source.height || source.width>16384 || source.height>16384 || source.pixels.size()!=size_t(source.width)*source.height*4) throw std::runtime_error("Invalid texture pixels");
        if (std::max(source.width,source.height)>settings.maxSize) {
            const double factor=double(settings.maxSize)/std::max(source.width,source.height);
            source=Resize(source,std::max(1u,unsigned(source.width*factor)),std::max(1u,unsigned(source.height*factor)),settings.srgb);
        }
        std::vector<TextureLevel> levels; levels.push_back(std::move(source));
        if(settings.mipmaps) while(levels.back().width>1 || levels.back().height>1) levels.push_back(Resize(levels.back(),std::max(1u,levels.back().width/2),std::max(1u,levels.back().height/2),settings.srgb));
        if(Compressed(settings,levels.front())) for(auto& level:levels) level=Compress(level);
        return levels;
    }
};
}
