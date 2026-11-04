// Reproduce Pimax report: Wait -> Locate -> render/release -> Begin -> End.
#define main submission_regression_main
#include "frame_submission_test.cpp"
#undef main
namespace {
unsigned begins=0;
XrResult beginResult=XR_SUCCESS;
XrResult XRAPI_CALL lateBegin(XrSession,const XrFrameBeginInfo*){++begins;return beginResult;}
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void pending(Fixture& f){
    clearFrame();s.runtimeSettings=true;s.running=true;s.waitPending=true;s.waitShouldRender=true;
    s.pendingTime=f.info.displayTime;s.pendingPeriod=11111111;s.locateTime=s.pendingTime;s.located=true;
    for(auto& outer:s.outer)outer.acquired=outer.waited=false;
    next.BeginFrame=lateBegin;begins=0;beginResult=XR_SUCCESS;
}
}
int main(){try{
    Fixture f;pending(f);
    XrView views[4]{};
    require(lmuqv::xr_frame_views(views),"late BeginFrame: render views unavailable after successful Wait/Locate");
    require(begins==1&&s.frame,"runtime frame was not begun before capture");
    s.captures[0]=s.captures[1]=true;s.sceneFocus[0]=s.sceneFocus[1]=true;
    for(auto& outer:s.outer)outer.acquired=outer.waited=true;
    require(lmuqv::xr_frame_views(views)&&begins==1,"second eye began the frame again");
    require(layerEndFrame(s.session,&f.info)==XR_ERROR_CALL_ORDER_INVALID&&runtimeEnds==0&&releases==0,"End accepted before application Begin");
    auto frame=s.frames;
    // The next Wait may finish while the app is still about to acknowledge Begin.
    s.waitPending=true;s.pendingTime=f.info.displayTime+100;s.waitShouldRender=false;
    require(layerBeginFrame(s.session,nullptr)==XR_SUCCESS,"application begin failed");
    require(begins==1&&s.frames==frame&&s.captures[0]&&s.captures[1]&&s.renderViewsLocked&&s.sceneFocus[0],"late application begin reset rendered captures");
    require(s.waitPending&&s.pendingTime==f.info.displayTime+100&&s.predictedTime==f.info.displayTime&&s.shouldRender,"pipelined wait replaced current frame");
    f.submit();require(received.projectionCount==1&&received.projection.viewCount==4&&releases==2,"late-begin frame lost projection or image ownership");
    require(layerBeginFrame(s.session,nullptr)==XR_SUCCESS&&begins==2&&!s.shouldRender&&!s.located,"next waited frame was lost");
    for(int mode=0;mode<6;++mode){
        pending(f);
        if(mode==0)s.waitShouldRender=false;
        if(mode==1)s.waitPending=false;
        if(mode==2)s.locateTime++;
        if(mode==3)s.located=false;
        if(mode==4)s.runtimeSettings=false;
        if(mode==5)s.focusBeginInFlight=true;
        require(!lmuqv::xr_frame_views(views)&&begins==0,"invalid pending render triggered runtime Begin");
        s.focusBeginInFlight=false;
    }
    for(auto result:{XR_ERROR_RUNTIME_FAILURE,XR_FRAME_DISCARDED}){
        pending(f);beginResult=result;
        XrFovf fovs[2]{};
        require(lmuqv::xr_frame_full_outer_fov(fovs)==XR_SUCCEEDED(result)&&begins==1,"early Begin result mishandled");
        require(lmuqv::xr_frame_views(views)==XR_SUCCEEDED(result)&&begins==1,"failed/discarded early begin retried");
        require(layerBeginFrame(s.session,nullptr)==result&&begins==1,"app did not receive original runtime result");
    }
    std::cout<<"Late BeginFrame regression PASS: one runtime begin, captures preserved, four views submitted\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
