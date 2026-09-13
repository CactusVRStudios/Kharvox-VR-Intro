#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <compressapi.h>
#include <cstdio>
#include <vector>
#include <cstdint>
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 1;
    FILE* file{};if(_wfopen_s(&file,argv[1],L"rb"))return 1;
    fseek(file,0,SEEK_END);const auto size=ftell(file);rewind(file);
    if(size<=0||size>1024*1024){fclose(file);return 1;}
    std::vector<unsigned char> original(size);const auto read=fread(original.data(),1,size,file);fclose(file);
    if(read!=size_t(size))return 1;
    auto best=original;DWORD algorithm=0;
    for(DWORD candidate:{COMPRESS_ALGORITHM_MSZIP,COMPRESS_ALGORITHM_XPRESS_HUFF,COMPRESS_ALGORITHM_LZMS}){
        COMPRESSOR_HANDLE compressor{};if(!CreateCompressor(candidate,nullptr,&compressor))return 1;
        SIZE_T needed{};Compress(compressor,original.data(),original.size(),nullptr,0,&needed);
        std::vector<unsigned char> packed(needed);
        const bool ok=needed&&Compress(compressor,original.data(),original.size(),packed.data(),packed.size(),&needed);
        CloseCompressor(compressor);if(!ok)return 1;packed.resize(needed);
        DECOMPRESSOR_HANDLE decoder{};if(!CreateDecompressor(candidate,nullptr,&decoder))return 1;
        std::vector<unsigned char> restored(size);SIZE_T written{};
        const bool valid=Decompress(decoder,packed.data(),packed.size(),restored.data(),restored.size(),&written);
        CloseDecompressor(decoder);if(!valid||written!=original.size()||restored!=original)return 1;
        if(packed.size()<best.size()){best=std::move(packed);algorithm=candidate;}
    }
    if(_wfopen_s(&file,argv[2],L"wb"))return 1;
    const uint32_t header[]={algorithm,uint32_t(size)};
    const bool ok=fwrite(header,1,sizeof(header),file)==sizeof(header)&&fwrite(best.data(),1,best.size(),file)==best.size();
    fclose(file);std::printf("Asset: %ld -> %zu bytes, codec %lu; byte-exact roundtrip verified\n",size,best.size()+8,algorithm);
    return ok?0:1;
}
