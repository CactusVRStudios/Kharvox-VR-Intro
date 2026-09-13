#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/intro/CubeScene.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using namespace kharvox::intro;
static void check(bool ok){if(!ok)throw std::runtime_error("Quest asset baking failed");}
int main(int argc,char** argv){try{
    check(argc==2);std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    Scene scene;check(scene.makeText());
    std::ofstream file(out/"text.bin",std::ios::binary);
    auto write=[&](auto value){file.write(reinterpret_cast<const char*>(&value),sizeof(value));};
    write(uint32_t(0x32545851));
    for(unsigned block=0;block<=Scene::blockCount;block++){
        const auto& quads=block==Scene::blockCount?scene.footerQuads:scene.textQuads[block];
        write(uint32_t(quads.size()));
        std::vector<std::array<int,6>> packed;
        for(auto q:quads){
            const float baseY=block==Scene::blockCount?scene.eyeLocalY-1.f:0.f;
            std::array<int,6> p={int(std::lround(q.x/.0022f)),int(std::lround((q.y-baseY)/.0022f)),
                int(std::lround(q.width/.0044f)),int(std::lround(q.height/.0044f)),int(q.mode),int(q.line)};

            check(std::abs(p[0]*.0022f-q.x)<.00001f&&std::abs(p[1]*.0022f+baseY-q.y)<.00001f);
            check(std::abs(p[2]*.0044f-q.width)<.00001f&&std::abs(p[3]*.0044f-q.height)<.00001f);
            packed.push_back(p);
        }
        for(unsigned field=0;field<6;field++){
            int previous=0;
            for(const auto& p:packed){const int delta=p[field]-previous;check(delta>=-32768&&delta<=32767);write(int16_t(delta));previous=p[field];}
        }
    }
    check(bool(file));file.close();
    check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));
    ComPtr<IWICImagingFactory> imaging;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&imaging))));
    auto png=asset(104);ComPtr<IWICStream> stream;check(SUCCEEDED(imaging->CreateStream(&stream)));
    check(SUCCEEDED(stream->InitializeFromMemory((BYTE*)png.data,DWORD(png.size))));
    ComPtr<IWICBitmapDecoder> decoder;check(SUCCEEDED(imaging->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)));
    ComPtr<IWICBitmapFrameDecode> frame;check(SUCCEEDED(decoder->GetFrame(0,&frame)));
    UINT w,h;check(SUCCEEDED(frame->GetSize(&w,&h)));
    ComPtr<IWICFormatConverter> converter;check(SUCCEEDED(imaging->CreateFormatConverter(&converter)));
    check(SUCCEEDED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)));
    std::vector<unsigned char> rgba(w*h*4);check(SUCCEEDED(converter->CopyPixels(nullptr,w*4,UINT(rgba.size()),rgba.data())));
    std::ofstream copper(out/"copper.bin",std::ios::binary);
    uint32_t dimensions[2]={w,h};copper.write((char*)dimensions,sizeof(dimensions));copper.write((char*)rgba.data(),rgba.size());check(bool(copper));
    std::cout<<"Baked original text and copper palette: "<<w<<"x"<<h<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
