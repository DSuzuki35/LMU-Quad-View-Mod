#include "../native_stereo_scene_plan.hpp"
#include <cassert>
#include <iostream>
#include "allocation_failure_fixture.hpp"
using namespace lmuqv;
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
NativeStereoSceneInput fixture(){
    NativeStereoSceneInput in;in.identity={1,2,3,400,5};in.scene=10;in.views.complete=true;in.views.identity=in.identity;in.views.scene=10;
    in.owners.owners.resize(3);in.world.assign(3,identity());in.buckets={NativeStereoBucketOrder::Append};
    in.owners.owners[0].children={1,2};in.owners.owners[1].state.mask=2;
    in.views.eyes[0].owner.mask=1;in.views.eyes[1].owner.mask=3;
    for(auto& eye:in.views.eyes){eye.classification.viewScale=1;eye.classification.secondaryViewRadius=1000;}
    in.views.eyes[1].classification.viewPosition={0,0,20};
    for(unsigned i=0;i<3;++i){NativeStereoSceneSource s;s.id=(i+1)*100;s.owner=i;s.meshes={i*10+1};
        s.candidate.candidateFlags=0x8000;s.candidate.radius=1;s.candidate.farCoefficient=1000;
        s.sortingPosition=s.localPosition={0,0,float(i==0?5:i==1?4:8)};
        in.owners.owners[i].sourceIds={s.id};in.sources.push_back(s);}
    in.sources[0].meshes={11,12};return in;
}
void empty(const NativeStereoScenePlan& p){assert(!p.complete&&!p.renderingEligible&&p.eyes[0].empty()&&p.eyes[1].empty()&&p.effects.empty()&&p.finalSourceFlags.empty()&&!p.owners.complete);}
int main(){
    NativeStereoFpGuard defaults(true);auto in=fixture();auto out=prepare_native_stereo_scene(in);
    assert(out.complete&&!out.renderingEligible&&out.eyes[0].size()==3&&out.eyes[1].size()==4&&out.positionCalculations==0);
    assert(out.eyes[0][0].source==100&&out.eyes[0][2].source==300&&out.eyes[1][2].source==200);
    assert(out.eyes[0][0].squaredDistance==25&&out.eyes[1][0].squaredDistance==225);
    assert(out.owners.eyes[0].emissionStates.size()==out.owners.eyes[0].ownerOrder.size());
    in.owners.owners[0].children={1};in.owners.roots={2,0};out=prepare_native_stereo_scene(in);
    assert(out.complete&&out.eyes[0][0].source==300&&out.eyes[1][0].source==300&&out.eyes[1].back().source==200);
    in.owners.roots={2,2};empty(prepare_native_stereo_scene(in));
    in.owners.roots={0};empty(prepare_native_stereo_scene(in));
    in=fixture();out=prepare_native_stereo_scene(in);
    unsigned finalizers=0,submissions=0;for(auto e:out.effects){finalizers+=e.kind==NativeStereoSceneEffectKind::FinalizeSource;submissions+=e.kind==NativeStereoSceneEffectKind::OwnerSubmission;}
    assert(finalizers==5&&submissions==5);
    in.buckets[0]=NativeStereoBucketOrder::NearToFar;out=prepare_native_stereo_scene(in);
    assert(out.complete&&out.eyes[0][0].source==100&&out.eyes[1][0].source==300&&out.eyes[1].back().source==200);
    in.buckets[0]=NativeStereoBucketOrder::FarToNear;out=prepare_native_stereo_scene(in);
    assert(out.complete&&out.eyes[0][0].source==300&&out.eyes[1][0].source==200);
    in=fixture();in.owners.owners[0].state.flags=0x40000000;in.sources[0].candidate.candidateFlags|=0x40;in.world[0][12]=10;
    out=prepare_native_stereo_scene(in);assert(out.complete&&out.positionCalculations==1&&!(out.finalSourceFlags[0]&0x40));
    assert(out.eyes[0][0].squaredDistance==125&&out.eyes[1][0].squaredDistance==325);
    assert(out.owners.eyes[0].emissionStates[0].flags&0x40000000);
    assert(!(out.owners.eyes[1].emissionStates[0].flags&0x40000000));
    in=fixture();in.sources[1].candidate.farCoefficient=10;out=prepare_native_stereo_scene(in);assert(out.complete&&out.eyes[1].size()==3);
    in=fixture();in.sources[0].positionKind=NativeStereoPositionKind::IndependentPoint;
    in.sources[0].candidate.position={0,0,2};in.sources[0].sortingPosition={0,0,50};in.sources[0].candidate.farCoefficient=10;
    in.views.eyes[1].classification.viewPosition={};out=prepare_native_stereo_scene(in);
    assert(out.complete&&out.eyes[0][0].squaredDistance==2500); // sorting point is not classifier R8
    in=fixture();in.sources[0].candidate.candidateFlags|=0x20;out=prepare_native_stereo_scene(in);
    assert(out.complete&&out.eyes[0].size()==1); // native finalization precedes this append mask
    finalizers=0;for(auto e:out.effects)if(e.source==100&&e.kind==NativeStereoSceneEffectKind::FinalizeSource)++finalizers;assert(finalizers==2);
    in=fixture();in.sources[0].candidate.candidateFlags|=0x1000000;out=prepare_native_stereo_scene(in);
    assert(out.complete&&out.eyes[0].size()==1&&(out.finalSourceFlags[0]&0x40000));
    in=fixture();in.sources[0].candidate.planeCount=5;
    for(auto& v:in.views.eyes){v.classification.viewMode=1;v.classification.planes[4]={0,0,1,0};}
    out=prepare_native_stereo_scene(in);assert(out.complete&&out.eyes[0][0].source==100); // native stored flags, not the distinct special-plane return
    in=fixture();in.sources[0].candidate.planeCount=1;in.views.eyes[0].classification.planes[0]={1,0,0,0};
    empty(prepare_native_stereo_scene(in)); // native intersection preparation is still required
    in.owners.owners[0].state.flags=0x80000000;assert(prepare_native_stereo_scene(in).complete);
    for(unsigned failure=0;failure<10;++failure){in=fixture();
        switch(failure){
        case 0:++in.views.identity.frame;break;
        case 1:in.sources[1].id=100;break;
        case 2:in.sources[1].owner=0;break;
        case 3:in.sources[0].bucket=1;break;
        case 4:in.sources[0].meshes={1,1};break;
        case 5:in.world.pop_back();break;
        case 6:in.owners.owners[0].state.flags=0x400000;break;
        case 7:in.owners.owners[0].state.flags=0x40000;break;
        case 8:in.sources[0].candidate.radius=-1;break;
        default:in.sources[0].sortingPosition.z=std::numeric_limits<float>::quiet_NaN();break;
        }empty(prepare_native_stereo_scene(in));
    }
    in=fixture();in.buckets[0]=NativeStereoBucketOrder::NearToFar;in.sources[2].sortingPosition=in.sources[0].sortingPosition;empty(prepare_native_stereo_scene(in));
    in=fixture();_mm_setcsr(0x1fbf);assert(prepare_native_stereo_scene(in).complete&&_mm_getcsr()==0x1fbf);
    _mm_setcsr(0x9f80);empty(prepare_native_stereo_scene(in));assert(_mm_getcsr()==0x9f80);_mm_setcsr(0x1f80);
    in=fixture();allocation_refusals([&]{return prepare_native_stereo_scene(in);},[](const auto& p,bool complete){if(complete)assert(p.complete);else empty(p);});
    in=fixture();in.sources.clear();in.owners.owners.assign(1114,{});in.world.assign(1114,identity());
    for(uint32_t i=0;i<1114;++i){in.owners.roots.push_back(1113-i);in.owners.owners[i].sourceIds={i};
        NativeStereoSceneSource s;s.id=i;s.owner=i;s.meshes={0};s.candidate.candidateFlags=0x8000;s.candidate.radius=1;s.candidate.farCoefficient=1000;
        s.sortingPosition={0,0,10};in.sources.push_back(s);}
    out=prepare_native_stereo_scene(in);assert(out.complete&&out.eyes[0].size()==1114&&out.eyes[1].size()==1114&&out.eyes[0][0].source==1113&&out.eyes[1].back().source==0);
    std::cout<<"Owned stereo scene: independent admissions/classification/order, shared source caches, native effects, special stored/returned flags, callback refusals and FP preservation passed\n";
}
