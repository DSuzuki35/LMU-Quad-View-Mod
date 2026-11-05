#include "../native_stereo_focus_inputs.hpp"
#include <cassert>
#include <iostream>
#include <limits>

using namespace lmuqv;
namespace {
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
NativeStereoWorldPoseInput input(){
    NativeStereoWorldPoseInput in;in.origin={100,-20,30};in.offset={.5f,.25f,-.75f};
    // Ninety-degree world rotation. X=-offsetY, Y=offsetX, Z=offsetZ.
    in.rotation={0,-1,0,1,0,0,0,0,1};
    in.scalars[0]={0,.2f,0,-.03125f,.125f,-.25f};
    in.scalars[1]={0,.2f,0,.03125f,-.125f,.25f};return in;
}
NativeStereoCameraSnapshot camera(const Matrix& current,const NativeStereoWorldPoseInput& in){
    NativeStereoCameraSnapshot c;c.complete=true;c.eye=0;c.cameraMode=0;c.transformMode=1;
    c.current=current;c.scalars=in.scalars;c.relativeEye={identity(),identity()};
    c.projection=c.farProjection={1,0,0,0,0,1,0,0,0,0,0,1,0,0,.1f,0};
    c.engineProjection=c.engineFarProjection={c.projection,c.projection};return c;
}
}
int main(){
    _mm_setcsr(0x1f80);const auto in=input();
    // Expected results are independently calculated from signed offsets and
    // the quarter-turn above, including distinct Y/Z translations per eye.
    const std::array<float,3> left{99.875f,-19.53125f,29.5f},right{99.625f,-19.46875f,29};
    Matrix current={0,-1,0,0,1,0,0,0,0,0,1,0,left[1],-left[0],left[2],1};
    NativeStereoWorldPoseFrame producer;assert(producer.begin(77,in));
    assert(producer.finish(77,current,left,1,0));auto c=camera(current,in);
    auto pair=producer.take(77,c);assert(pair.complete&&pair.constantPosition[0]==left&&pair.constantPosition[1]==right);
    assert(pair.current[0]==current&&pair.current[1][12]==right[1]&&pair.current[1][13]==-right[0]&&pair.current[1][14]==right[2]);
    assert(!producer.take(77,c).complete); // A completed producer is single-use.
    for(unsigned failure=0;failure<7;++failure){
        assert(producer.begin(77,in));auto changed=current;auto position=left;auto mode=1u,flags=0u;uintptr_t view=77;
        if(failure==0)++view;else if(failure==1)position[0]+=1;else if(failure==2)changed[12]+=1;
        else if(failure==3)mode=2;else if(failure==4)flags=2;else if(failure==5)changed[0]=2;else changed[3]=1;
        assert(!producer.finish(view,changed,position,int32_t(mode),flags));assert(!producer.take(77,c).complete);
    }
    // No translation from the other frame/view may slip through a stale match.
    assert(producer.begin(77,in));assert(producer.finish(77,current,left,1,0));
    assert(!producer.take(78,c).complete);auto wrong=c;wrong.scalars[1][3]+=.01f;assert(!producer.take(77,wrong).complete);
    wrong=c;wrong.current[12]+=.1f;assert(!producer.take(77,wrong).complete);
    wrong=c;wrong.cameraMode=1;assert(!producer.take(77,wrong).complete);
    wrong=c;wrong.translation[1]=.1f;assert(!producer.take(77,wrong).complete);
    wrong=c;wrong.euler[0]=.1f;assert(!producer.take(77,wrong).complete);
    assert(producer.take(77,c).complete);
    auto bad=in;bad.scalars[1][1]=.3f;assert(!producer.begin(77,bad));
    bad=in;bad.rotation[1]=std::numeric_limits<float>::quiet_NaN();assert(!producer.begin(77,bad));
    bad=in;bad.offset[0]=std::numeric_limits<float>::max();bad.scalars[1][3]=bad.offset[0];assert(!producer.begin(77,bad));
    _mm_setcsr(0x3f80);assert(!producer.begin(77,in));assert(_mm_getcsr()==0x3f80);_mm_setcsr(0x1f80);
    // The zero-offset branch skips rotation, but the native view-translation
    // addition still changes -0 + +0 into +0. Negative-zero offsets retain -0.
    NativeStereoWorldPoseInput zero;zero.origin={-0.f,0.f,-0.f};zero.rotation={1,0,0,0,1,0,0,0,1};
    std::array<std::array<float,3>,2> positions{};assert(native_world_pose_detail::positions(zero,positions));
    const std::array<float,3> positiveZero{};assert(native_world_pose_detail::same(positions[0],positiveZero));
    zero.viewTranslation={-0.f,0.f,-0.f};assert(native_world_pose_detail::positions(zero,positions));
    assert(native_world_pose_detail::same(positions[0],zero.origin));
    // Focus preparation consumes the direct native pair, with owned constant
    // positions. It must never silently substitute the inverse-eye prediction.
    QuadFrameInputs frame;frame.identity={1,2,3,400,5};
    for(auto& v:frame.views){v.type=XR_TYPE_VIEW;v.pose.orientation.w=1;v.fov={-.3f,.4f,.2f,-.2f};}
    c.scalars[0][1]=c.scalars[1][1]=0;
    auto focus=prepare_native_stereo_focus_inputs(frame,c,.1f,1000,1,1,&pair);
    assert(focus.complete&&focus.nativeWorldPose&&!focus.renderingEligible);
    assert(focus.eyes[1].current==pair.current[1]&&focus.eyes[1].constantPosition==right);
    auto inconsistent=pair;inconsistent.constantPosition[1][0]+=1;
    assert(!prepare_native_stereo_focus_inputs(frame,c,.1f,1000,1,1,&inconsistent).complete);
    pair.current[0][12]+=1;assert(!prepare_native_stereo_focus_inputs(frame,c,.1f,1000,1,1,&pair).complete);
    std::cout<<"native world pose: direct independent eye offsets, exact left checks, focus integration and refusal passed\n";
}
