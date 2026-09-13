#include "QuestAssets.h"
#include "../src/intro/CubeScene.h"
#include <cstring>
namespace kharvox::intro {
bool Scene::makeText(){
    const auto bytes=quest::readAsset("text.bin");size_t offset=0;
    auto read=[&](auto& value){
        if(offset+sizeof(value)>bytes.size())throw std::runtime_error("Truncated text geometry");
        std::memcpy(&value,bytes.data()+offset,sizeof(value));offset+=sizeof(value);
    };
    uint32_t magic;read(magic);if(magic!=0x32545851)throw std::runtime_error("Invalid text geometry");
    for(unsigned block=0;block<=blockCount;block++){
        auto& out=block==blockCount?footerQuads:textQuads[block];
        uint32_t count;read(count);if(!count||count>50000)throw std::runtime_error("Invalid text count");
        out.clear();out.reserve(count);
        std::vector<std::array<int,6>> packed(count);
        for(unsigned field=0;field<6;field++){
            int previous=0;
            for(auto& p:packed){int16_t delta;read(delta);previous+=delta;if(previous< -32768||previous>32767)throw std::runtime_error("Invalid text delta");p[field]=previous;}
        }
        for(uint32_t i=0;i<count;i++){
            const auto& p=packed[i];const int x=p[0],y=p[1],w=p[2],h=p[3],mode=p[4],line=p[5];
            if(mode<0||mode>1||w<=0||h<=0||line<0||(block<blockCount&&unsigned(line)>=blocks()[block].size()))throw std::runtime_error("Invalid text rectangle");
            const auto tint=block==blockCount?footerColor:mode?copperBase:outlineColor;
            out.push_back({x*.0022f,y*.0022f+(block==blockCount?eyeLocalY-1.f:0.f),textZ,
                w*.0044f,h*.0044f,color(tint),float(mode),unsigned(line)});
        }
        if(block<blockCount){
            textLineY[block].resize(blocks()[block].size());
            for(unsigned line=0;line<blocks()[block].size();line++){
                float low=10,high=-10;
                for(const auto& q:out)if(q.line==line){low=std::min(low,q.y-q.height*.5f);high=std::max(high,q.y+q.height*.5f);}
                textLineY[block][line]=(low+high)*.5f;
            }
        }
    }
    return offset==bytes.size();
}
}
