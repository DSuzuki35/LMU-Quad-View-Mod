#define main submission_regression_main
#include "frame_submission_test.cpp"
#undef main
#include <atomic>
#include <thread>
namespace {
bool quadRuntime=true;
XrResult locateResult=XR_SUCCESS;
unsigned locateCalls=0;
XrResult XRAPI_CALL locateUnlocked(XrSession,const XrViewLocateInfo* info,XrViewState* state,uint32_t,uint32_t* count,XrView* views){
    std::atomic<bool> available{false};
    std::thread observer([&]{if(stateMutex.try_lock()){available=true;stateMutex.unlock();}});observer.join();
    if(!available)throw std::runtime_error("LocateViews held stateMutex");
    XrView engineViews[4]{};
    if(layerBeginFrame(s.session,nullptr)!=XR_ERROR_CALL_ORDER_INVALID||
       layerEndSession(s.session)!=XR_ERROR_CALL_ORDER_INVALID||
       layerDestroySession(s.session)!=XR_ERROR_CALL_ORDER_INVALID||
       layerDestroyInstance(s.instance)!=XR_ERROR_CALL_ORDER_INVALID||
       lmuqv::xr_frame_views(engineViews))throw std::runtime_error("Locate reservation did not protect session/frame");
    ++locateCalls;
    // A following Wait may complete during tracking, just as during Begin.
    {std::lock_guard<std::recursive_mutex> lock(stateMutex);s.waitPending=true;s.pendingTime=info->displayTime+100;s.waitShouldRender=false;}
    if(XR_FAILED(locateResult))return locateResult;
    *count=quadRuntime?4:2;state->viewStateFlags=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    for(unsigned i=0;i<*count;++i){views[i].pose.orientation.w=1;views[i].fov=i<2?XrFovf{-.8f,.8f,.7f,-.7f}:XrFovf{-.4f,.4f,.3f,-.3f};}
    return XR_SUCCESS;
}
}
int main(){try{
    Fixture fixture;next.LocateViews=locateUnlocked;
    XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};info.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    info.space=s.locateSpace;info.displayTime=s.predictedTime;
    XrViewState state{XR_TYPE_VIEW_STATE};XrView views[2]{};uint32_t count=0;
    assert(layerLocateViews(s.session,&info,&state,2,&count,views)==XR_SUCCESS&&count==2&&s.located&&!s.locateInFlight);
    assert(s.waitPending&&s.pendingTime==info.displayTime+100&&s.predictedTime==info.displayTime);
    locateResult=XR_ERROR_RUNTIME_FAILURE;
    assert(layerLocateViews(s.session,&info,&state,2,&count,views)==locateResult&&!s.locateInFlight);
    locateResult=XR_SUCCESS;quadRuntime=false;s.quad=false;s.vrs=true;s.requestEyeTracking=false;
    assert(layerLocateViews(s.session,&info,&state,2,&count,views)==XR_SUCCESS&&s.located&&!s.locateInFlight);
    s.vrs=false;
    assert(layerLocateViews(s.session,&info,&state,2,&count,views)==XR_SUCCESS&&locateCalls==4&&!s.locateInFlight);
    std::cout<<"Locate runtime lock regression PASS: quad/VRS/trace unlocked, failure cleared, pipelined Wait preserved\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
