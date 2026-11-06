#include "../native_stereo_owner_plan.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
#include "allocation_failure_fixture.hpp"
using namespace lmuqv;
namespace {
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
NativeStereoOwnerWorld fixture(){
    NativeStereoOwnerWorld w;w.available=true;w.local=w.world=identity();w.scale={1,1,1};
    for(unsigned i=0;i<8;++i)w.localCorners[i]={i&1?2.f:-2.f,i&2?3.f:-3.f,i&4?6.f:-6.f};
    return w;
}
bool exact(float a,float b){return std::memcmp(&a,&b,4)==0;}
void empty(const NativeStereoOwnerPlanResult& p){
    assert(!p.complete&&!p.renderingEligible&&p.finalWorld.empty()&&p.finalStates.empty()&&p.transitions.empty()&&p.worldRefreshes==0);
    for(const auto& eye:p.eyes)assert(eye.ownerOrder.empty()&&eye.emissionWorld.empty());
}
NativeStereoOwnerPlanInput tree(){
    NativeStereoOwnerPlanInput in;in.owners.resize(4);in.worldInputs.assign(4,fixture());
    in.owners[0].children={1,2};in.owners[2].children={3};in.owners[0].state.flags=0x400000;
    in.worldInputs[0].local[12]=10;in.owners[1].state.mask=2;
    in.worldInputs[1].local[12]=20;in.worldInputs[2].local[12]=30;in.worldInputs[3].local[12]=40;
    in.owners[3].state.attachmentPresent=true;in.worldInputs[3].attachmentIndex=2;
    in.views[0].mask=1;in.views[1].mask=3;
    return in;
}
}
int main(){
    NativeStereoFpGuard defaults(true);auto w=fixture();w.local[12]=10;const auto before=w;
    auto out=refresh_native_stereo_world(w,0x480000,false);
    assert(out.complete&&!out.skipped&&out.flags==0x40880000&&out.world.center.x==10&&out.world.center.y==0&&out.world.radius==7);
    assert(out.world.world==w.local&&out.world.worldCorners[0].x==8&&out.world.worldCorners[7].z==6);
    assert(!std::memcmp(&before,&w,sizeof(w)));
    // Scaling applies even to projective terms; the homogeneous row is retained.
    w=fixture();w.localMetadata=7;w.scale={2,3,4};w.local[3]=2;w.local[12]=11;
    out=refresh_native_stereo_world(w,0x400000,false);
    assert(out.complete&&out.world.localMetadata==2&&out.world.local[0]==2&&out.world.local[5]==3&&out.world.local[10]==4&&
        out.world.local[3]==4&&out.world.local[12]==11);
    w.localMetadata=2;assert(refresh_native_stereo_world(w,0x400000,false).world.local==w.local);
    // Attachment world multiplication and bounds use the parent's corners,
    // although this owner's own corners still receive its full transform.
    auto parent=refresh_native_stereo_world(fixture(),0,false).world;parent.world[12]=100;
    w=fixture();w.local[12]=10;out=refresh_native_stereo_world(w,0x400000,true,&parent);
    assert(out.complete&&out.world.world[12]==110&&out.world.worldCorners[0].x==108&&out.world.center.x==0&&out.world.radius==7);
    w.externalAttachmentAvailable=true;w.attachmentWorld=parent.world;w.attachmentCorners=parent.worldCorners;
    assert(refresh_native_stereo_world(w,0x400000,true).world.world==out.world.world);
    w.externalAttachmentAvailable=false;assert(!refresh_native_stereo_world(w,0,true).complete);
    for(auto flags:{0x1000000u,0x20000000u})assert(!refresh_native_stereo_world(w,flags,false).complete);
    w.sceneFlags=4;assert(!refresh_native_stereo_world(w,0x200000,false).complete);
    w.sceneFlags=0;assert(refresh_native_stereo_world(w,0x200000,false).complete);
    // The disabled gate is before unsupported paths and before missing inputs.
    out=refresh_native_stereo_world({},0x21020000,false);assert(out.complete&&out.skipped&&out.flags==0x21020000);
    assert(!refresh_native_stereo_world({},0x21020000,false,nullptr,true).complete);
    w=fixture();w.local[0]=std::numeric_limits<float>::infinity();assert(!refresh_native_stereo_world(w,0,false).complete);
    w=fixture();w.local[0]=std::numeric_limits<float>::max();w.localCorners[0].x=4;assert(!refresh_native_stereo_world(w,0,false).complete);
    w=fixture();w.localCorners[0].x=std::numeric_limits<float>::quiet_NaN();assert(!refresh_native_stereo_world(w,0,false).complete);
    w=fixture();_mm_setcsr(0x1fbf);assert(refresh_native_stereo_world(w,0,false).complete&&_mm_getcsr()==0x1fbf);
    _mm_setcsr(0x9f80);assert(!refresh_native_stereo_world(w,0,false).complete&&_mm_getcsr()==0x9f80);_mm_setcsr(0x1f80);
    // Independent high precision reference for 1,000 finite affine inputs.
    std::mt19937 rng(191);std::uniform_real_distribution<float> distribution(-10,10);
    for(unsigned sample=0;sample<1000;++sample){
        w=fixture();for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)w.local[r*4+c]=distribution(rng);
        out=refresh_native_stereo_world(w,0,false);assert(out.complete);
        double maximum=0;
        for(unsigned i=0;i<8;++i){const auto& p=w.localCorners[i];const auto& actual=out.world.worldCorners[i];
            const float values[]{actual.x,actual.y,actual.z};const double input[]{p.x,p.y,p.z};
            for(unsigned c=0;c<3;++c){double expected=w.local[12+c];for(unsigned r=0;r<3;++r)expected+=input[r]*w.local[r*4+c];
                assert(std::abs(double(values[c])-expected)<.00005);}
            const auto& center=out.world.center;const double dx=double(center.x)-actual.x,dy=double(center.y)-actual.y,dz=double(center.z)-actual.z;
            maximum=std::max(maximum,dx*dx+dy*dy+dz*dz);
        }
        assert(std::abs(double(out.world.radius)-std::sqrt(maximum))<.00005);
    }
    // Both eyes share refreshed state. A left-rejected child is nevertheless
    // refreshed by its admitted parent, and then admitted independently right.
    auto in=tree();auto plan=build_native_stereo_owner_plan(in);
    assert(plan.complete&&!plan.renderingEligible&&plan.worldRefreshes==4&&plan.finalWorld[3].world[12]==70);
    assert(plan.eyes[0].ownerOrder==std::vector<uint32_t>({0,2,3})&&plan.eyes[1].ownerOrder==std::vector<uint32_t>({0,1,2,3}));
    assert(plan.eyes[0].emissionWorld[2][12]==70&&plan.eyes[1].emissionWorld[1][12]==20);
    assert(plan.eyes[0].emissionStates[0].flags&0x40000000u);
    assert(!(plan.eyes[1].emissionStates[0].flags&0x40000000u));
    assert(in.owners[0].state.flags==0x400000&&in.worldInputs[3].world==identity());
    // A masked root never refreshes its children until an eye admits it.
    in.owners[0].state.mask=2;plan=build_native_stereo_owner_plan(in);
    assert(plan.complete&&plan.eyes[0].ownerOrder.empty()&&plan.worldRefreshes==4&&plan.eyes[1].emissionWorld[3][12]==70);
    // Disabled child skips its entire refresh subtree, as in native recursion.
    in=tree();in.owners[2].state.flags=0x20000;in.worldInputs[3].available=false;
    plan=build_native_stereo_owner_plan(in);assert(plan.complete&&plan.worldRefreshes==2&&plan.finalWorld[3].world==identity());
    in=tree();in.worldInputs[2].available=false;empty(build_native_stereo_owner_plan(in));
    in=tree();in.worldInputs[3].attachmentIndex=4;empty(build_native_stereo_owner_plan(in));
    in=tree();in.owners[3].state.flags=0x1000000;empty(build_native_stereo_owner_plan(in));
    in=tree();allocation_refusals([&]{return build_native_stereo_owner_plan(in);},[](const auto& p,bool complete){if(complete)assert(p.complete);else empty(p);});
    // Reach the bounds gate using refreshed corners, not stale copied bounds.
    in=tree();in.owners[0].state.flags|=0x80000;in.worldInputs[0].local[12]=0;
    for(auto& eye:in.views){eye.bounds.available=true;eye.bounds.range=100;
        eye.bounds.planes={{{1,0,0,-100},{-1,0,0,-100},{0,1,0,-100},{0,-1,0,-100},{0,0,1,-100},{0,0,-1,-100}}};}
    assert(build_native_stereo_owner_plan(in).complete);
    in.views[0].bounds.position={1000,0,0};plan=build_native_stereo_owner_plan(in);
    assert(plan.complete&&plan.eyes[0].ownerOrder.empty()&&plan.eyes[1].ownerOrder.size()==4&&plan.worldRefreshes==4);
    // Signed zero in native four-term scale multiplication is observable.
    w=fixture();w.localMetadata=6;w.scale={-1,0,1};w.local[1]=-0.f;
    out=refresh_native_stereo_world(w,0,false);assert(out.complete&&exact(out.world.local[1],0.f));
    std::cout<<"Native ordinary world refresh: scale/product/corners/radius, attachments, refresh-before-admission, shared eye state and atomic refusal passed\n";
}
