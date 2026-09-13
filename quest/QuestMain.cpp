#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/window.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include "QuestRenderer.h"
#include "QuestAudio.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"VRCracktro",__VA_ARGS__)
static void check(XrResult result,const char* what){if(XR_FAILED(result))throw std::runtime_error(std::string(what)+": "+std::to_string(result));}
static double seconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}

struct QuestIntro {
    android_app* app{};bool resumed{},running{},focused{},visible{},stopping{},exitRequested{},presented{};
    bool firstFrame{};double start{},exitAt{},lastStats{};unsigned frames{};
    XrInstance instance{};XrSystemId system{};XrSession session{};XrSpace local{},stage{};
    XrActionSet actions{};XrAction buttons{},analogs{};
    EGLDisplay display=EGL_NO_DISPLAY;EGLContext context=EGL_NO_CONTEXT;EGLSurface surface=EGL_NO_SURFACE;
    EGLConfig config{};
    struct Eye {XrSwapchain chain{};int width{},height{};std::vector<XrSwapchainImageOpenGLESKHR> images;};
    std::array<Eye,2> eyes;
    kharvox::intro::Scene scene;quest::Renderer renderer;quest::Audio audio;
    explicit QuestIntro(android_app* source):app(source){
        app->userData=this;
        app->onAppCmd=[](android_app* app,int32_t cmd){
            auto& self=*static_cast<QuestIntro*>(app->userData);
            if(cmd==APP_CMD_RESUME)self.resumed=true;
            if(cmd==APP_CMD_PAUSE)self.resumed=false;
            LOG("Android command %d resumed=%d",cmd,self.resumed);
        };
        ANativeActivity_setWindowFlags(app->activity,AWINDOW_FLAG_KEEP_SCREEN_ON,0);
    }
    void pump(int timeout){
        int events;android_poll_source* source{};
        while(ALooper_pollOnce(timeout,nullptr,&events,reinterpret_cast<void**>(&source))>=0){
            if(source)source->process(app,source);
            if(app->destroyRequested)break;timeout=0;
        }
    }
    XrPath path(const char* name){XrPath result;check(xrStringToPath(instance,name,&result),"Input path");return result;}
    void initialize(){
        PFN_xrInitializeLoaderKHR init{};
        check(xrGetInstanceProcAddr(XR_NULL_HANDLE,"xrInitializeLoaderKHR",reinterpret_cast<PFN_xrVoidFunction*>(&init)),"Loader entry");
        XrLoaderInitInfoAndroidKHR loader{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loader.applicationVM=app->activity->vm;loader.applicationContext=app->activity->clazz;
        check(init(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&loader)),"Initialize Android loader");
        const char* extensions[]={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
        XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
        android.applicationVM=app->activity->vm;android.applicationActivity=app->activity->clazz;
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};info.next=&android;
        std::strcpy(info.applicationInfo.applicationName,"KHARVOX Intro");info.applicationInfo.applicationVersion=1;
        info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);info.enabledExtensionCount=2;info.enabledExtensionNames=extensions;
        check(xrCreateInstance(&info,&instance),"Create OpenXR instance");
        XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};check(xrGetInstanceProperties(instance,&properties),"Runtime properties");
        LOG("Runtime: %s",properties.runtimeName);
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        check(xrGetSystem(instance,&systemInfo,&system),"Find headset");
        PFN_xrGetOpenGLESGraphicsRequirementsKHR getRequirements{};
        check(xrGetInstanceProcAddr(instance,"xrGetOpenGLESGraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements)),"GLES requirements entry");
        XrGraphicsRequirementsOpenGLESKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        check(getRequirements(instance,system,&requirements),"GLES requirements");
        display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if(display==EGL_NO_DISPLAY||!eglInitialize(display,nullptr,nullptr)||!eglBindAPI(EGL_OPENGL_ES_API))throw std::runtime_error("EGL display failed");
        const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,0,EGL_STENCIL_SIZE,0,EGL_NONE};
        EGLint count{};if(!eglChooseConfig(display,attrs,&config,1,&count)||!count)throw std::runtime_error("No EGL config");
        const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx);
        const EGLint size[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};surface=eglCreatePbufferSurface(display,config,size);
        if(context==EGL_NO_CONTEXT||surface==EGL_NO_SURFACE||!eglMakeCurrent(display,surface,surface,context))throw std::runtime_error("GLES context failed");
        GLint major{},minor{};glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
        const auto version=XR_MAKE_VERSION(major,minor,0);
        if(version<requirements.minApiVersionSupported||version>requirements.maxApiVersionSupported)throw std::runtime_error("Unsupported GLES version");
        LOG("GLES %s; GPU %s",glGetString(GL_VERSION),glGetString(GL_RENDERER));
        XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};binding.display=display;binding.config=config;binding.context=context;
        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};sessionInfo.next=&binding;sessionInfo.systemId=system;
        check(xrCreateSession(instance,&sessionInfo,&session),"Create headset session");
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;space.poseInReferenceSpace.orientation.w=1;
        check(xrCreateReferenceSpace(session,&space,&local),"Create local space");
        space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;
        if(XR_FAILED(xrCreateReferenceSpace(session,&space,&stage)))stage=XR_NULL_HANDLE;
        setupInput();
        uint32_t viewsCount{};check(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&viewsCount,nullptr),"Count views");
        if(viewsCount!=2)throw std::runtime_error("Stereo views required");
        std::array<XrViewConfigurationView,2> views{{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        check(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&viewsCount,views.data()),"View dimensions");
        uint32_t formatsCount;check(xrEnumerateSwapchainFormats(session,0,&formatsCount,nullptr),"Count formats");
        std::vector<int64_t> formats(formatsCount);check(xrEnumerateSwapchainFormats(session,formatsCount,&formatsCount,formats.data()),"Swapchain formats");
        int64_t format=0;for(auto candidate:{GL_RGBA8,GL_SRGB8_ALPHA8})if(std::find(formats.begin(),formats.end(),candidate)!=formats.end()){format=candidate;break;}
        if(!format)throw std::runtime_error("RGBA swapchain unavailable");
        for(unsigned e=0;e<2;e++){
            auto& eye=eyes[e];eye.width=views[e].recommendedImageRectWidth;eye.height=views[e].recommendedImageRectHeight;
            XrSwapchainCreateInfo swap{XR_TYPE_SWAPCHAIN_CREATE_INFO};swap.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            swap.format=format;swap.sampleCount=1;swap.width=eye.width;swap.height=eye.height;swap.faceCount=1;swap.arraySize=1;swap.mipCount=1;
            check(xrCreateSwapchain(session,&swap,&eye.chain),"Create eye swapchain");
            uint32_t n;check(xrEnumerateSwapchainImages(eye.chain,0,&n,nullptr),"Count eye images");eye.images.resize(n,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
            check(xrEnumerateSwapchainImages(eye.chain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())),"Eye images");
            LOG("Eye %u: %dx%d, %u images, format %lld",e,eye.width,eye.height,n,(long long)format);
        }
        quest::assets=app->activity->assetManager;
        if(!scene.makeText())throw std::runtime_error("Text geometry unavailable");
        renderer.initialize();audio.initialize();start=lastStats=seconds();LOG("Ready: original intro, MOD music, native GLES, arm64");
    }
    void setupInput(){
        XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};std::strcpy(set.actionSetName,"intro");std::strcpy(set.localizedActionSetName,"Intro");
        check(xrCreateActionSet(instance,&set,&actions),"Create actions");
        XrActionCreateInfo action{XR_TYPE_ACTION_CREATE_INFO};action.actionType=XR_ACTION_TYPE_BOOLEAN_INPUT;
        std::strcpy(action.actionName,"exit_button");std::strcpy(action.localizedActionName,"Exit button");check(xrCreateAction(actions,&action,&buttons),"Create exit button");
        action.actionType=XR_ACTION_TYPE_FLOAT_INPUT;std::strcpy(action.actionName,"exit_trigger");std::strcpy(action.localizedActionName,"Exit trigger");
        check(xrCreateAction(actions,&action,&analogs),"Create exit trigger");
        std::vector<XrActionSuggestedBinding> bindings;
        for(const char* hand:{"left","right"}){
            const std::string base=std::string("/user/hand/")+hand+"/input/";
            for(const auto* button:{hand[0]=='l'?"x/click":"a/click",hand[0]=='l'?"y/click":"b/click","thumbstick/click"})bindings.push_back({buttons,path((base+button).c_str())});
            for(const auto* trigger:{"trigger/value","squeeze/value"})bindings.push_back({analogs,path((base+trigger).c_str())});
        }
        XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggested.interactionProfile=path("/interaction_profiles/oculus/touch_controller");suggested.countSuggestedBindings=uint32_t(bindings.size());suggested.suggestedBindings=bindings.data();
        check(xrSuggestInteractionProfileBindings(instance,&suggested),"Touch bindings");
        const XrActionSuggestedBinding simple[]={{buttons,path("/user/hand/left/input/select/click")},{buttons,path("/user/hand/right/input/select/click")}};
        suggested.interactionProfile=path("/interaction_profiles/khr/simple_controller");suggested.countSuggestedBindings=2;suggested.suggestedBindings=simple;
        check(xrSuggestInteractionProfileBindings(instance,&suggested),"Simple bindings");
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&actions;
        check(xrAttachSessionActionSets(session,&attach),"Attach inputs");
    }
    bool exitInput(){
        if(!focused||!presented)return false;
        XrActiveActionSet set{actions,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&set;
        const auto result=xrSyncActions(session,&sync);if(result==XR_SESSION_NOT_FOCUSED)return false;check(result,"Sync inputs");
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=buttons;XrActionStateBoolean button{XR_TYPE_ACTION_STATE_BOOLEAN};
        check(xrGetActionStateBoolean(session,&info,&button),"Button input");info.action=analogs;XrActionStateFloat analog{XR_TYPE_ACTION_STATE_FLOAT};
        check(xrGetActionStateFloat(session,&info,&analog),"Trigger input");
        return (button.isActive&&button.currentState)||(analog.isActive&&analog.currentState>.55f);
    }
    void events(){
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};XrResult result;
        while((result=xrPollEvent(instance,&event))==XR_SUCCESS){
            if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
                const auto state=reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
                focused=state==XR_SESSION_STATE_FOCUSED;visible=focused||state==XR_SESSION_STATE_VISIBLE;
                LOG("XR state=%d",state);
                if(state==XR_SESSION_STATE_READY){
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    check(xrBeginSession(session,&begin),"Begin session");running=true;
                }
                if(state==XR_SESSION_STATE_STOPPING){audio.active(false);check(xrEndSession(session),"End session");running=false;if(exitRequested)stopping=true;}
                if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING)stopping=true;
            }else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)stopping=true;
            else if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING){scene.anchored=false;LOG("Reference space changed; re-anchor on next tracked frame");}
            event={XR_TYPE_EVENT_DATA_BUFFER};
        }
        check(result,"Poll events");
    }
    void frame(){
        const double cpuStart=seconds();
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState state{XR_TYPE_FRAME_STATE};check(xrWaitFrame(session,&wait,&state),"Wait frame");
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};check(xrBeginFrame(session,&begin),"Begin frame");
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=state.predictedDisplayTime;locate.space=local;
        std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};XrViewState tracked{XR_TYPE_VIEW_STATE};uint32_t n{};
        check(xrLocateViews(session,&locate,&tracked,2,&n,views.data()),"Locate eyes");
        const bool valid=n==2&&(tracked.viewStateFlags&XR_VIEW_STATE_POSITION_VALID_BIT)&&(tracked.viewStateFlags&XR_VIEW_STATE_ORIENTATION_VALID_BIT);
        if(valid&&!scene.anchored){
            float floorY=NAN;XrSpaceLocation floor{XR_TYPE_SPACE_LOCATION};
            if(stage&&XR_SUCCEEDED(xrLocateSpace(stage,local,state.predictedDisplayTime,&floor))&&(floor.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT))floorY=floor.pose.position.y;
            scene.setAnchor(views,floorY);if(!firstFrame)start=seconds();LOG("Anchored; floor=%f",floorY);
        }
        if(!exitRequested&&exitInput()){
            audio.active(false);check(xrRequestExitSession(session),"Request exit");exitRequested=true;exitAt=seconds();LOG("Controller exit requested");
        }
        const bool draw=valid&&state.shouldRender&&resumed;
        const float time=float(seconds()-start);
        size_t quads=0;if(draw&&!exitRequested)quads=renderer.prepare(scene,time,audio.beatAge());
        std::array<XrCompositionLayerProjectionView,2> projection{{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
        if(draw)for(unsigned e=0;e<2;e++){
            auto& eye=eyes[e];uint32_t image;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};check(xrAcquireSwapchainImage(eye.chain,&acquire,&image),"Acquire eye");
            XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};imageWait.timeout=XR_INFINITE_DURATION;check(xrWaitSwapchainImage(eye.chain,&imageWait),"Wait eye");
            if(image>=eye.images.size())throw std::runtime_error("Invalid eye image");
            renderer.draw(eye.images[image].image,eye.width,eye.height,scene,views[e],time,exitRequested);
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};check(xrReleaseSwapchainImage(eye.chain,&release),"Release eye");
            auto& out=projection[e];out.pose=views[e].pose;out.fov=views[e].fov;out.subImage.swapchain=eye.chain;out.subImage.imageRect.extent={eye.width,eye.height};
        }
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=local;layer.viewCount=2;layer.views=projection.data();
        const auto* header=reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer);
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=state.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount=draw?1:0;end.layers=draw?&header:nullptr;check(xrEndFrame(session,&end),"Submit frame");
        if(draw&&!exitRequested){presented=true;if(!firstFrame){LOG("First stereo frame submitted; %zu quads",quads);firstFrame=true;}}
        audio.active(firstFrame&&visible&&resumed&&!exitRequested);
        ++frames;if(seconds()-lastStats>=10){LOG("Frames=%u rate=%.1f lastFrame=%.2fms block=%u quads=%zu",frames,frames/(seconds()-lastStats),(seconds()-cpuStart)*1000,scene.blockIndex(time),quads);frames=0;lastStats=seconds();}
    }
    void run(){
        while(!app->destroyRequested&&(!resumed||!app->window))pump(100);
        if(app->destroyRequested)return;
        initialize();
        while(!app->destroyRequested&&!stopping){
            pump(running?0:20);if(app->destroyRequested)break;events();if(stopping)break;
            if(exitRequested&&seconds()-exitAt>5)break;
            if(running)frame();else audio.active(false);
        }
    }
    ~QuestIntro(){
        audio.shutdown();
        if(context!=EGL_NO_CONTEXT){glFinish();renderer.shutdown();}
        for(auto& eye:eyes)if(eye.chain)xrDestroySwapchain(eye.chain);
        if(stage)xrDestroySpace(stage);if(local)xrDestroySpace(local);
        if(session)xrDestroySession(session);if(actions)xrDestroyActionSet(actions);if(instance)xrDestroyInstance(instance);
        if(display!=EGL_NO_DISPLAY){eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
            if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);eglTerminate(display);}
        app->userData=nullptr;app->onAppCmd=nullptr;LOG("Shutdown complete: audio, GLES and OpenXR released");
    }
};
void android_main(android_app* app){
    JNIEnv* env{};const bool attached=app->activity->vm->AttachCurrentThread(&env,nullptr)==JNI_OK;
    try{if(!attached)throw std::runtime_error("Attach JVM failed");QuestIntro intro(app);intro.run();}
    catch(const std::exception& error){__android_log_print(ANDROID_LOG_ERROR,"VRCracktro","FATAL: %s",error.what());}
    ANativeActivity_finish(app->activity);

    while(!app->destroyRequested){int events;android_poll_source* source{};if(ALooper_pollOnce(100,nullptr,&events,(void**)&source)>=0&&source)source->process(app,source);}
    if(attached)app->activity->vm->DetachCurrentThread();
}

