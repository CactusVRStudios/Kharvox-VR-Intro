#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <openxr/openxr.h>
#include <cstdio>
#include <vector>
int main(){
    auto getProc=&xrGetInstanceProcAddr;
    PFN_xrEnumerateApiLayerProperties enumerate{};
    bool ok=getProc&&XR_SUCCEEDED(getProc(XR_NULL_HANDLE,"xrEnumerateApiLayerProperties",reinterpret_cast<PFN_xrVoidFunction*>(&enumerate)))&&enumerate;
    uint32_t count{};
    if(ok)ok=XR_SUCCEEDED(enumerate(0,&count,nullptr));
    if(ok&&count){std::vector<XrApiLayerProperties> layers(count,{XR_TYPE_API_LAYER_PROPERTIES});ok=XR_SUCCEEDED(enumerate(count,&count,layers.data()));}
    std::printf("%s: statically linked loader entry point and API enumeration\n",ok?"PASS":"FAIL");
    return ok?0:1;
}
