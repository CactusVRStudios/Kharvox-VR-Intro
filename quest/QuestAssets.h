#pragma once
#include <android/asset_manager.h>
#include <vector>
#include <cstdint>
#include <stdexcept>
namespace quest {
inline AAssetManager* assets{};
inline std::vector<uint8_t> readAsset(const char* name){
    auto* file=AAssetManager_open(assets,name,AASSET_MODE_BUFFER);
    if(!file)throw std::runtime_error(name);
    const auto size=AAsset_getLength(file);
    if(size<=0||size>4*1024*1024){AAsset_close(file);throw std::runtime_error("Invalid asset size");}
    std::vector<uint8_t> bytes(size);
    const int read=AAsset_read(file,bytes.data(),bytes.size());AAsset_close(file);
    if(read!=size)throw std::runtime_error("Incomplete asset");
    return bytes;
}
}
