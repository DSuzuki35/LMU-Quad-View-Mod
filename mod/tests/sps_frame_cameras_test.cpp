#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../sps_frame_cameras.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <limits>

using namespace lmuqv;
namespace {
QuadFrameInputs input(uint64_t frame=10){
    QuadFrameInputs in;in.identity={1,frame,3,int64_t(frame*100),GetCurrentThreadId()};
    for(unsigned i=0;i<4;++i){auto& v=in.views[i];v.type=XR_TYPE_VIEW;v.pose.orientation.w=1;
        v.pose.position={i%2?.032f:-.032f,0,.1f};v.fov=i<2?XrFovf{-.7f,.6f,.5f,-.5f}:XrFovf{-.3f,.25f,.2f,-.2f};}
    return in;
}
std::array<float,4> transform(const std::array<float,4>& p,const Matrix& m){
    std::array<float,4> out{};for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)out[col]+=p[row]*m[row*4+col];return out;
}
bool closeEnough(float a,float b,float epsilon=1e-4f){return std::abs(a-b)<epsilon;}
}
int main(){
    SpsFrameCameras owner;auto in=input();SpsCameraPlacement placement;
    placement.nearClip=.1f;placement.farClip=100;placement.worldUnitsPerMeter=2;
    placement.worldFromTracking={0,1,0,0,-1,0,0,0,0,0,1,0,10,20,30,1};
    assert(owner.prepare(in,placement));const auto* frame=owner.get(in.identity);assert(frame&&!frame->historyValid);
    const auto first=*frame;
    for(unsigned i=0;i<4;++i){const auto& eye=frame->views[i];
        assert(closeEnough(eye.worldPosition[0],10)&&closeEnough(eye.worldPosition[1],i%2?20.064f:19.936f)&&closeEnough(eye.worldPosition[2],29.8f));
        for(unsigned axis=0;axis<3;++axis)assert(eye.constantPosition[axis]==-eye.worldPosition[axis]);
        const auto origin=transform({eye.worldPosition[0],eye.worldPosition[1],eye.worldPosition[2],1},eye.view);
        assert(closeEnough(origin[0],0)&&closeEnough(origin[1],0)&&closeEnough(origin[2],0)&&origin[3]==1);
        for(float distance:{placement.nearClip,1.f,placement.farClip}){
            const std::array<float,4> world{eye.worldPosition[0]+eye.worldFromEye[8]*distance,
                eye.worldPosition[1]+eye.worldFromEye[9]*distance,eye.worldPosition[2]+eye.worldFromEye[10]*distance,1};
            const auto clip=transform(world,eye.viewProjection);assert(clip[3]>0);
            assert(closeEnough(clip[0]/clip[3],eye.projection[8])&&closeEnough(clip[1]/clip[3],eye.projection[9]));
            if(distance==placement.nearClip)assert(closeEnough(clip[2]/clip[3],1));
            if(distance==placement.farClip)assert(closeEnough(clip[2]/clip[3],0));
        }
        assert(eye.previousViewProjection==eye.viewProjection);
    }
    assert(frame->views[0].view==frame->views[2].view&&frame->views[1].view==frame->views[3].view);
    assert(frame->views[0].projection!=frame->views[2].projection);
    // Mutating the producer's memory cannot alter an already-owned frame.
    auto changed=in;changed.views[1].pose.position.x=20;changed.views[3].fov.angleLeft=-1;
    assert(!owner.prepare(changed,placement)&&owner.get(in.identity)->views[1].view==first.views[1].view);
    bool foreignRead=true;std::thread other([&]{foreignRead=owner.get(in.identity)!=nullptr;});other.join();assert(!foreignRead);
    assert(owner.finish(in.identity)&&!owner.get(in.identity));assert(!owner.finish(in.identity));
    in=input(11);in.views[0].pose.position.y=.2f;in.views[2].pose=in.views[0].pose;
    assert(owner.prepare(in,placement));frame=owner.get(in.identity);assert(frame&&frame->historyValid);
    for(unsigned i=0;i<4;++i)assert(frame->views[i].previousViewProjection==first.views[i].viewProjection);
    assert(owner.finish(in.identity));
    // Session/space/camera changes and explicit cuts invalidate temporal history.
    for(unsigned change=0;change<4;++change){in=input(12+change);
        if(change==0)placement.cameraCut=true;
        if(change==1){placement.cameraCut=false;++placement.camera;}
        if(change==2)++in.identity.space;
        if(change==3)++in.identity.session;
        assert(owner.prepare(in,placement));assert(!owner.get(in.identity)->historyValid);assert(owner.finish(in.identity));
    }
    // Differently rotated/canted runtime eyes are independently built from
    // the same placement, rather than copied from the left camera basis.
    in=input(20);for(unsigned i=0;i<4;++i){in.views[i].pose.orientation.y=i%2?std::sin(.05f):-std::sin(.05f);in.views[i].pose.orientation.w=std::cos(.05f);}
    assert(owner.prepare(in,placement));frame=owner.get(in.identity);
    assert(frame->views[0].view!=frame->views[1].view);assert(owner.finish(in.identity));
    // SPS determines its own math controls and restores the caller unchanged.
    const auto saved=_mm_getcsr();const int rounding=std::fegetround();std::fesetround(FE_DOWNWARD);_mm_setcsr(0x9fc0);
    const int incomingRounding=std::fegetround();
    in=input(21);assert(owner.prepare(in,placement));assert(_mm_getcsr()==0x9fc0&&std::fegetround()==incomingRounding);
    owner.abort();std::fesetround(rounding);_mm_setcsr(saved);assert(!owner.get(in.identity));
    for(unsigned failure=0;failure<7;++failure){auto bad=input(30+failure);auto p=placement;
        if(failure==0)bad.views[0].pose.orientation.w=0;
        if(failure==1)bad.views[1].pose.position.x=std::numeric_limits<float>::quiet_NaN();
        if(failure==2)bad.views[3].next=&bad;
        if(failure==3)p.worldFromTracking[0]=2;
        if(failure==4)p.farClip=p.nearClip;
        if(failure==5)p.worldUnitsPerMeter=0;
        if(failure==6)bad.views[0].fov.angleLeft=2;
        assert(!owner.prepare(bad,p)&&!owner.get(bad.identity));
    }
    std::cout<<"SPS-only cameras: one immutable quad snapshot, analytic world placement/projection, independent eyes, history/cuts, thread ownership and caller FP isolation passed\n";
}
