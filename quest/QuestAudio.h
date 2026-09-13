#pragma once
#include <aaudio/AAudio.h>
#include <atomic>
#include <array>
#include "QuestAssets.h"
#include "../third-party/pocketmod/pocketmod.h"
#include "../src/intro/MusicBeat.h"
namespace quest {
class Audio {
    AAudioStream* stream{};std::vector<uint8_t> module;
    pocketmod_context decoder{};kharvox::intro::MusicBeat detector;
    std::atomic<int> failure{0};std::atomic<uint64_t> rendered{0};
    std::array<std::atomic<uint64_t>,64> beats{};unsigned beatIndex{};
    bool playing{};
    static aaudio_data_callback_result_t callback(AAudioStream*,void* self,void* data,int32_t frames){
        auto& a=*static_cast<Audio*>(self);int remaining=frames*2*sizeof(float);auto* cursor=static_cast<char*>(data);
        while(remaining){
            int n=pocketmod_render(&a.decoder,cursor,remaining);
            if(n<=0||n>remaining||n%8){std::memset(data,0,frames*8);a.failure=-1;return AAUDIO_CALLBACK_RESULT_STOP;}
            cursor+=n;remaining-=n;
        }
        auto* pcm=static_cast<float*>(data);
        for(int i=0;i<frames*2;i++)pcm[i]=std::isfinite(pcm[i])?std::clamp(pcm[i],-1.f,1.f):0.f;
        a.detector.consume(pcm,frames*2,[&](uint64_t f){a.beats[a.beatIndex++%a.beats.size()].store(f,std::memory_order_release);});
        for(int i=0;i<frames*2;i++)pcm[i]*=.65f;
        a.rendered.fetch_add(frames,std::memory_order_release);return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    static void error(AAudioStream*,void* self,aaudio_result_t result){static_cast<Audio*>(self)->failure=result;}
    static void check(aaudio_result_t result){if(result<0)throw std::runtime_error(AAudio_convertResultToText(result));}
public:
    void initialize(){
        module=readAsset("ninja.mod");
        if(!pocketmod_init(&decoder,module.data(),int(module.size()),48000))throw std::runtime_error("MOD initialization failed");
        AAudioStreamBuilder* builder{};check(AAudio_createStreamBuilder(&builder));
        AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);AAudioStreamBuilder_setFormat(builder,AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setChannelCount(builder,2);AAudioStreamBuilder_setSampleRate(builder,48000);
        AAudioStreamBuilder_setPerformanceMode(builder,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setUsage(builder,AAUDIO_USAGE_GAME);
        AAudioStreamBuilder_setDataCallback(builder,callback,this);AAudioStreamBuilder_setErrorCallback(builder,error,this);
        const auto result=AAudioStreamBuilder_openStream(builder,&stream);AAudioStreamBuilder_delete(builder);check(result);
        if(AAudioStream_getSampleRate(stream)!=48000||AAudioStream_getChannelCount(stream)!=2||AAudioStream_getFormat(stream)!=AAUDIO_FORMAT_PCM_FLOAT)
            throw std::runtime_error("Unsupported audio stream format");
    }
    void active(bool desired){
        if(failure.load())throw std::runtime_error("Android audio stream failed");
        if(desired==playing)return;
        check(desired?AAudioStream_requestStart(stream):AAudioStream_requestPause(stream));playing=desired;
    }
    float beatAge()const{
        if(!playing)return -1;

        int64_t position{},timestamp{};uint64_t played=rendered.load(std::memory_order_acquire);
        if(AAudioStream_getTimestamp(stream,CLOCK_MONOTONIC,&position,&timestamp)==AAUDIO_OK){
            timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
            const int64_t ns=int64_t(now.tv_sec)*1000000000+now.tv_nsec;
            played=std::min(played,uint64_t(std::max(int64_t(0),position+(ns-timestamp)*48000/1000000000)));
        }
        uint64_t latest=0;for(auto& beat:beats){auto f=beat.load(std::memory_order_acquire);if(f<=played)latest=std::max(latest,f);}
        return latest?float(played-latest)/48000.f:-1.f;
    }
    void shutdown(){if(stream){AAudioStream_requestStop(stream);AAudioStream_close(stream);stream=nullptr;}playing=false;}
    ~Audio(){shutdown();}
};
}
