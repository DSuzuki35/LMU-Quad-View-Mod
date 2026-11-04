#include "../native_stereo_focus_inputs.hpp"
#include <cassert>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

using namespace lmuqv;
namespace {
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
NativeStereoCameraSnapshot camera(){
    NativeStereoCameraSnapshot out;
    out.complete=true;out.eye=0;out.transformMode=1;out.cameraMode=0;
    out.current=identity();out.current[12]=10;out.current[13]=20;out.current[14]=30;
    out.relativeEye={identity(),identity()};out.relativeEye[0][12]=-.03f;out.relativeEye[1][12]=.03f;
    out.projection={2,0,0,0,0,3,0,0,.12f,-.08f,-.001f,1,0,0,.1f,0};
    out.farProjection=out.projection;out.farProjection[10]=-.0001f;out.farProjection[14]=.01f;
    out.engineProjection={out.projection,out.projection};
    out.engineProjection[1][0]=2.4f;out.engineProjection[1][8]=-.23f;
    out.engineFarProjection={out.farProjection,out.farProjection};
    out.engineFarProjection[1][0]=2.4f;out.engineFarProjection[1][8]=-.23f;
    out.derived=multiply(out.current,out.projection);return out;
}
QuadFrameInputs frame(){
    QuadFrameInputs out;out.identity={1,2,3,4000,5};
    for(unsigned i=0;i<4;++i){out.views[i].type=XR_TYPE_VIEW;out.views[i].pose.orientation.w=1;
        out.views[i].pose.position.x=i%2?.03f:-.03f;out.views[i].fov={-.8f,.7f,.6f,-.5f};}
    out.views[2].fov={-.35f,.27f,.25f,-.19f};out.views[3].fov={-.22f,.4f,.28f,-.2f};return out;
}
bool same(const Matrix& a,const Matrix& b){return !std::memcmp(a.data(),b.data(),sizeof(a));}
void refused(const QuadFrameInputs& f,const NativeStereoCameraSnapshot& c,const char* reason){
    const auto out=prepare_native_stereo_focus_inputs(f,c,.1f,10000.f);
    assert(!out.complete&&!out.renderingEligible&&!out.identity.valid());
    assert(std::string(out.reason).find(reason)!=std::string::npos);
    for(const auto& eye:out.eyes)for(float value:eye.projection)assert(value==0);
}
}
int main(){
    const unsigned saved=_mm_getcsr();_mm_setcsr(0x1fbf);
    auto f=frame();auto c=camera();auto out=prepare_native_stereo_focus_inputs(f,c,.1f,10000.f);
    assert(out.complete&&!out.renderingEligible&&out.identity==f.identity&&_mm_getcsr()==0x1fbf);
    assert(same(out.eyes[0].current,c.current));
    assert(std::abs(out.eyes[1].current[12]-10.06f)<1e-5f&&out.eyes[1].current[13]==20);
    for(unsigned eye=0;eye<2;++eye){
        const auto& p=out.eyes[eye].projection;const auto& far=out.eyes[eye].farProjection;
        const auto& v=f.views[eye+2];
        assert(std::abs(std::tan(v.fov.angleLeft)*p[0]+p[8]+1)<1e-5f);
        assert(std::abs(std::tan(v.fov.angleRight)*p[0]+p[8]-1)<1e-5f);
        assert(std::abs(std::tan(v.fov.angleDown)*p[5]+p[9]+1)<1e-5f);
        assert(std::abs(std::tan(v.fov.angleUp)*p[5]+p[9]-1)<1e-5f);
        for(unsigned i:{10u,11u,14u,15u}){assert(p[i]==c.projection[i]);assert(far[i]==c.farProjection[i]);}
        Matrix ordinary{},farOrdinary{};
        const Fov fv{v.fov.angleLeft,v.fov.angleRight,v.fov.angleUp,v.fov.angleDown};
        assert(make_engine_projection(fv,.1f,10000.f,1,1,ordinary));
        assert(make_engine_projection(fv,.1f,1000000.f,1,1,farOrdinary));
        assert(same(p,replace_fov(out.outerProjection[eye],ordinary)));
        assert(same(far,replace_fov(out.outerFarProjection[eye],farOrdinary)));
    }
    // Output owns its numbers; source mutation cannot change the prepared pair.
    f.views[3].fov.angleLeft=-.1f;c.current[12]=900;
    assert(out.eyes[1].projection[0]!=0&&std::abs(out.eyes[1].current[12]-10.06f)<1e-5f);
    // A rotated shared eye basis requires the translated rigid inverse.
    c=camera();for(auto& eye:c.relativeEye)eye={0,1,0,0,-1,0,0,0,0,0,1,0,0,0,0,1};
    c.relativeEye[0][12]=-.03f;c.relativeEye[1][12]=.03f;
    out=prepare_native_stereo_focus_inputs(frame(),c,.1f,10000.f);
    assert(out.complete&&std::abs(out.eyes[1].current[13]-19.94f)<1e-5f&&out.eyes[1].current[12]==10);
    f=frame();c=camera();
    auto scaled=prepare_native_stereo_focus_inputs(f,c,.1f,10000.f,.8f,.6f);
    assert(scaled.complete&&std::abs(std::tan(f.views[2].fov.angleLeft*.8f)*scaled.eyes[0].projection[0]+scaled.eyes[0].projection[8]+1)<1e-5f);
    // Failures publish no usable left-only output, including bad right FOVs.
    f.views[3].fov.angleRight=f.views[3].fov.angleLeft;refused(f,c,"projection");
    f=frame();f.views[3].pose.position.x+=.01f;refused(f,c,"pose mismatch");
    f=frame();f.views[2].pose.orientation.w=-1;
    assert(prepare_native_stereo_focus_inputs(f,c,.1f,10000.f).complete);
    f=frame();f.views[1].pose.orientation={0,.1f,0,std::sqrt(.99f)};f.views[3].pose=f.views[1].pose;
    refused(f,c,"canted");
    f=frame();f.views[3].pose.orientation={0,0,0,0};refused(f,c,"pose mismatch");
    f=frame();f.views[3].next=&f;refused(f,c,"header");
    f=frame();f.identity.session=0;refused(f,c,"identity");
    f=frame();c.eye=1;refused(f,c,"left outer");
    c=camera();c.cameraMode=3;refused(f,c,"camera path");
    c=camera();c.projection[11]=0;refused(f,c,"perspective");
    c=camera();c.current[12]=std::numeric_limits<float>::infinity();refused(f,c,"camera path");
    c=camera();
    for(float nearZ:{0.f,-1.f,1000000.f,std::numeric_limits<float>::quiet_NaN()})
        assert(!prepare_native_stereo_focus_inputs(frame(),c,nearZ,2000000.f).complete);
    assert(!prepare_native_stereo_focus_inputs(frame(),c,.1f,10000.f,0,1).complete);
    _mm_setcsr(0x3f80);assert(!prepare_native_stereo_focus_inputs(frame(),c,.1f,10000.f).complete&&_mm_getcsr()==0x3f80);
    _mm_setcsr(0x1f80);
    // Consumption enforces the same frame, scene, view, thread and eye order.
    NativeStereoFocusFrame prepared;f=frame();
    assert(prepared.prepare(f,c,11,22,.1f,10000.f));
    const auto input=*prepared.inputs(f.identity);Matrix focus{},far{};
    assert(prepared.consume(0,f.identity,11,22,input.outerProjection[0],input.outerFarProjection[0],focus,far));
    assert(same(focus,input.eyes[0].projection));
    assert(prepared.consume(1,f.identity,11,22,input.outerProjection[1],input.outerFarProjection[1],focus,far));
    assert(same(focus,input.eyes[1].projection));
    assert(!prepared.consume(1,f.identity,11,22,input.outerProjection[1],input.outerFarProjection[1],focus,far));
    assert(!prepared.inputs(f.identity));
    assert(prepared.prepare(f,c,11,22,.1f,10000.f));
    assert(!prepared.consume(1,f.identity,11,22,input.outerProjection[1],input.outerFarProjection[1],focus,far));
    for(unsigned field=0;field<5;++field){
        assert(prepared.prepare(f,c,11,22,.1f,10000.f));auto key=f.identity;
        if(field==0)++key.session;else if(field==1)++key.frame;else if(field==2)++key.space;else if(field==3)++key.displayTime;else ++key.thread;
        assert(!prepared.inputs(key));focus.fill(99);far.fill(88);
        assert(!prepared.consume(0,key,11,22,input.outerProjection[0],input.outerFarProjection[0],focus,far));
        assert(!prepared.inputs(f.identity)&&focus[0]==99&&far[0]==88);
    }
    for(unsigned field=0;field<3;++field){
        assert(prepared.prepare(f,c,11,22,.1f,10000.f));auto changed=input.outerProjection[0];if(field==2)changed[14]+=.01f;
        assert(!prepared.consume(0,f.identity,field==0?12:11,field==1?23:22,changed,input.outerFarProjection[0],focus,far));
    }
    assert(!prepared.prepare(f,c,0,22,.1f,10000.f));
    std::cout<<"Owned SPS focus inputs: both early focus FOVs, native depth preservation, translated/rotated camera, flat ownership, domain refusal and frame/view/eye-order fallback passed\n";
    _mm_setcsr(saved);
}
