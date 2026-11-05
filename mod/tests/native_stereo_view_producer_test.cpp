#include "../native_stereo_view_producer.hpp"
#include <cassert>
#include <iostream>

using namespace lmuqv;
namespace {
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
template<class T>void put(void* base,size_t offset,const T& value){std::memcpy(static_cast<std::byte*>(base)+offset,&value,sizeof(value));}
struct Fixture {
    std::array<std::byte,0x5d0> view{};
    NativeStereoFocusInputs focus;
    NativeStereoWorldPosePair pose;
    Fixture(){
        focus.complete=focus.nativeWorldPose=true;focus.identity={1,2,3,400,5};pose.complete=true;
        pose.settings={true,7,1,2,100,{.25f,-.5f,50}};
        for(unsigned eye=0;eye<2;++eye){
            auto& f=focus.eyes[eye];f.current=identity();f.current[12]=3+float(eye)*.25f;
            assert(make_engine_projection({-.3f,.4f,.2f,-.2f},.1f,100,1,1,f.projection));
            pose.current[eye]=f.current;pose.constantPosition[eye]={f.current[12],0,0};
        }
    }
    void native(unsigned eye){
        view={};const auto& f=focus.eyes[eye];const auto& s=pose.settings;
        Matrix inverse;assert(native_stereo_view_inverse(f.current,inverse));
        const auto geometry=prepare_native_stereo_view_geometry(f.current,f.projection);assert(geometry.complete);
        NativeStereoClassifyFloat3 bounds;assert(native_stereo_bounds_position(f.current,s.anchor,bounds));
        const auto& p=pose.constantPosition[eye];const NativeStereoClassifyFloat3 constant{p[0],p[1],p[2]},classification{-p[0],-p[1],-p[2]};
        put(view.data(),0x3e0,uint64_t(99));put(view.data(),0x5cc,int32_t(eye));put(view.data(),0x1dc,f.current);
        put(view.data(),0x320,inverse);put(view.data(),0x360,f.projection);put(view.data(),0x3a0,geometry.derived);
        put(view.data(),0x178,geometry.planes);put(view.data(),0x34,classification);put(view.data(),0x220,constant);
        put(view.data(),0x58,bounds);put(view.data(),0x4c,s.anchor);put(view.data(),0x10c,s.mask);
        put(view.data(),0x3e8,s.mode);put(view.data(),0x114,s.scale);put(view.data(),0x100,s.range);
    }
};
Fixture* fixture=nullptr;size_t calls=0,failAt=0;
bool readable(const void* p,size_t n){
    ++calls;if(failAt&&calls==failAt)return false;
    const auto a=reinterpret_cast<uintptr_t>(p),b=reinterpret_cast<uintptr_t>(fixture->view.data());
    return a>=b&&a-b<=fixture->view.size()&&n<=fixture->view.size()-(a-b);
}
NativeStereoViewObservation capture(Fixture& f){fixture=&f;return capture_native_stereo_view_observation(f.focus.identity,99,f.view.data(),readable);}
}
int main(){
    NativeStereoFpGuard defaults(true);Fixture f;NativeStereoViewInputFrame producer;
    const auto token=reinterpret_cast<uintptr_t>(f.view.data());
    assert(producer.prepare(f.focus,f.pose,99,token)&&producer.accepts(f.focus.identity));
    assert(!producer.inputs(f.focus.identity));f.native(0);const auto nativeCopy=f.view;
    calls=0;const auto left=capture(f);const auto readCount=calls;
    assert(left.complete&&left.bytesRead==nativeStereoViewObservationByteLimit&&f.view==nativeCopy);
    assert(producer.observe(left));const auto* inputs=producer.inputs(f.focus.identity);assert(inputs&&!inputs->renderingEligible);
    assert(inputs->eyes[0].classification.viewPosition.x==-3&&inputs->eyes[0].transform.position[0]==3);
    assert(inputs->eyes[0].owner.bounds.position.x!=inputs->eyes[1].owner.bounds.position.x);
    assert(inputs->eyes[0].owner.bounds.position.z!=inputs->eyes[0].classification.viewPosition.z);
    f.native(1);const auto right=capture(f);assert(producer.observe(right)&&producer.both_observed());
    assert(!producer.observe(right)&&!producer.inputs(f.focus.identity));
    // Numeric inverse/plane equations have independent tests and saved native
    // comparisons. This fixture checks the producer's complete input contract.
    for(unsigned failure=0;failure<17;++failure){
        assert(producer.prepare(f.focus,f.pose,99,token)&&producer.observe(left));auto changed=right;
        switch(failure){
        case 0:changed.current[12]+=1;break;case 1:changed.projection[0]+=1;break;
        case 2:changed.inverse[0]+=1;break;case 3:changed.derived[0]+=1;break;
        case 4:changed.planes[0].w+=1;break;case 5:changed.constantPosition.x+=1;break;
        case 6:changed.classifyPosition.x+=1;break;case 7:changed.boundsPosition.x+=1;break;
        case 8:changed.anchor.x+=1;break;case 9:++changed.mode;break;case 10:changed.mask^=1;break;
        case 11:changed.range+=1;break;case 12:changed.scale+=1;break;
        case 13:++changed.scene;break;case 14:++changed.view;break;case 15:changed.eye=0;break;
        default:++changed.identity.frame;break;
        }
        assert(!producer.observe(changed)&&!producer.inputs(f.focus.identity));
    }
    assert(producer.prepare(f.focus,f.pose,99,token)&&!producer.observe(right));
    auto changed=f.pose;changed.settings.complete=false;assert(!producer.prepare(f.focus,changed,99,token));
    changed=f.pose;changed.settings.range=-1;assert(!producer.prepare(f.focus,changed,99,token));
    changed=f.pose;changed.settings.anchor.x=NAN;assert(!producer.prepare(f.focus,changed,99,token));
    changed=f.pose;changed.current[1][12]+=1;assert(!producer.prepare(f.focus,changed,99,token));
    auto badFocus=f.focus;badFocus.nativeWorldPose=false;assert(!producer.prepare(badFocus,f.pose,99,token));
    for(size_t read=1;read<=readCount;++read){calls=0;failAt=read;f.native(0);const auto out=capture(f);
        assert(!out.complete&&!out.identity.valid()&&out.view==0&&out.bytesRead<=nativeStereoViewObservationByteLimit);}
    failAt=0;f.native(0);put(f.view.data(),0x4b0,uint64_t(1));assert(!capture(f).complete);
    f.native(0);_mm_setcsr(0x1fbf);assert(producer.prepare(f.focus,f.pose,99,token)&&producer.observe(capture(f))&&_mm_getcsr()==0x1fbf);
    _mm_setcsr(0x9f80);assert(!producer.prepare(f.focus,f.pose,99,token)&&_mm_getcsr()==0x9f80);
    std::cout<<"Native view producer: separate position domains, complete left/right comparison, bounded copies, settings/identity/order refusal and FP preservation passed\n";
}
