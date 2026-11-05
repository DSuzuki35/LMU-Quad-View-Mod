// Verify submission storage and ownership while a downstream EndFrame blocks.
// Fake runtime only: no device, native hooks or headset.
#define main submission_regression_main
#include "frame_submission_test.cpp"
#undef main
#include <atomic>
#include <thread>
#include <stdexcept>

namespace {
constexpr XrTime followingTime=23456;
void require(bool valid,const char* why){if(!valid)throw std::runtime_error(why);}
XrResult XRAPI_CALL followingWait(XrSession,const XrFrameWaitInfo*,XrFrameState* state){
    state->predictedDisplayTime=followingTime;state->predictedDisplayPeriod=22222222;state->shouldRender=XR_FALSE;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL unlockedEnd(XrSession session,const XrFrameEndInfo* info){
    std::atomic<bool> available{false};
    std::thread observer([&]{if(stateMutex.try_lock()){available=true;stateMutex.unlock();}});observer.join();
    require(available,"downstream EndFrame held stateMutex");
    require(s.submitInFlight,"submission lifetime was not reserved");
    XrView views[4]{};XrFovf fovs[2]{};uint32_t count=0;
    require(layerBeginFrame(session,nullptr)==XR_ERROR_CALL_ORDER_INVALID&&
        layerEndFrame(session,nullptr)==XR_ERROR_CALL_ORDER_INVALID&&
        layerLocateViews(session,nullptr,nullptr,0,&count,nullptr)==XR_ERROR_CALL_ORDER_INVALID&&
        layerEndSession(session)==XR_ERROR_CALL_ORDER_INVALID&&
        layerDestroySession(session)==XR_ERROR_CALL_ORDER_INVALID&&
        layerDestroyInstance(s.instance)==XR_ERROR_CALL_ORDER_INVALID&&
        layerCreateSwapchain(session,nullptr,nullptr)==XR_ERROR_CALL_ORDER_INVALID&&
        layerEnumerateSwapchainImages(XR_NULL_HANDLE,0,&count,nullptr)==XR_ERROR_CALL_ORDER_INVALID&&
        layerDestroySwapchain(XR_NULL_HANDLE)==XR_ERROR_CALL_ORDER_INVALID,
        "callback reentry could mutate submitted resource ownership");
    require(!lmuqv::xr_frame_views(views)&&!lmuqv::xr_frame_full_outer_fov(fovs)&&!lmuqv::xr_outer_rtv(0),
        "engine accessed a frame during submission");
    // This legal pipelined Wait must remain available after current-frame cleanup.
    XrFrameState state{XR_TYPE_FRAME_STATE};next.WaitFrame=followingWait;
    require(layerWaitFrame(session,nullptr,&state)==XR_SUCCESS,"pipelined Wait failed");
    require(s.waitPending&&s.pendingTime==followingTime&&s.predictedTime==12345,
        "pipelined Wait changed the submitted frame");
    return fakeEnd(session,info); // Inspect local arrays after reentry, while live.
}
}
int main(){try{
    for(unsigned mode=0;mode<3;++mode)for(bool large:{false,true})for(auto result:{XR_SUCCESS,XR_ERROR_RUNTIME_FAILURE}){
        Fixture f;s.quad=mode==0;s.vrs=mode==2;
        f.info.layerCount=large?17:3;
        for(unsigned i=0;i<f.info.layerCount;++i)f.layers[i]=Fixture::header(f.overlays[i%2]);
        f.layers[1]=Fixture::header(f.projection);
        next.EndFrame=unlockedEnd;downstreamResult=result;
        const auto returned=layerEndFrame(s.session,&f.info);
        require(returned==result&&!s.submitInFlight&&!s.frame,"submission result/reservation not retired");
        require(s.waitPending&&s.pendingTime==followingTime&&s.pendingPeriod==22222222&&!s.waitShouldRender,
            "submission cleanup erased the following Wait");
        require(received.info.layerCount==f.info.layerCount&&received.info.next==f.info.next&&
            received.info.displayTime==f.info.displayTime&&received.info.environmentBlendMode==f.info.environmentBlendMode,
            "submission metadata changed");
        if(mode==0)f.checkConversion();
        else require(received.original==&f.info&&received.projection.viewCount==2&&releases==0&&auditEnds==0,
            "trace/VRS submission changed forwarding semantics");
        require(runtimeEnds==1,"EndFrame forwarded more than once");
    }
    std::cout<<"EndFrame lock regression PASS: unlocked runtime submission, pinned metadata, callback guards, pipelined Wait, error cleanup\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
