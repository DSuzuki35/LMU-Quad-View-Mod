#include "../native_stereo_scene_inputs.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include "allocation_failure_fixture.hpp"
using namespace lmuqv;
namespace {
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
struct Fixture {
    std::array<std::array<uint8_t,0x438>,3> owners{};
    std::array<std::array<uint8_t,0x180>,3> sources{};
    std::array<std::array<uint8_t,0xe0>,3> meshes{};
    std::array<std::array<uint64_t,1>,3> pointers{};
    std::array<uint64_t,2> children{};
    std::array<uint32_t,3> lazyFlags{};
    std::array<uint8_t,0xa00> scene{};
    NativeStereoClassifyFloat3 independent{0,0,50};
    template<class T>void put(void* base,size_t offset,const T& value){std::memcpy(static_cast<uint8_t*>(base)+offset,&value,sizeof(value));}
    void vector(void* base,size_t offset,uint64_t begin,size_t count){put(base,offset,begin);put(base,offset+8,begin+8*count);put(base,offset+16,begin+8*count);}
    Fixture(){
        for(unsigned i=0;i<3;++i){
            auto* owner=owners[i].data();auto* source=sources[i].data();const auto geometry=uint64_t(reinterpret_cast<uintptr_t>(source));
            put(owner,0xec,1.f);put(owner,0x230,identity());put(owner,0x298,identity());
            put(owner,0x3f8,uint64_t(reinterpret_cast<uintptr_t>(scene.data())));
            put(owner,0x28c,NativeStereoClassifyFloat3{1,1,1});put(owner,0x74,uint32_t(i==1?2:0));
            pointers[i][0]=geometry;vector(owner,0x88,reinterpret_cast<uintptr_t>(pointers[i].data()),1);
            put(source,0,uint32_t(0x8000));put(source,0xdc,1.f);put(source,0x100,1000.f);
            const NativeStereoClassifyFloat3 p{0,0,float(10*(i+1))};put(source,0xe0,p);put(source,0xec,p);
            put(source,0x110,geometry+0xec);put(source,0x130,uint64_t(reinterpret_cast<uintptr_t>(owner)));
            put(source,0x168,int32_t(1));put(source,0x170,uint64_t(reinterpret_cast<uintptr_t>(meshes[i].data())));
            put(meshes[i].data(),0x80,geometry);
        }
        children={reinterpret_cast<uintptr_t>(owners[1].data()),reinterpret_cast<uintptr_t>(owners[2].data())};
        vector(owners[0].data(),0xa0,reinterpret_cast<uintptr_t>(children.data()),2);
    }
};
Fixture* active=nullptr;uintptr_t blocked=0;bool throws=false;
const std::vector<NativeStereoBucketOrder> bucketOrders{NativeStereoBucketOrder::Append};
bool readable(const void* address,size_t bytes){
    if(throws)throw 1;
    const auto p=reinterpret_cast<uintptr_t>(address),begin=reinterpret_cast<uintptr_t>(active);
    return p!=blocked&&p>=begin&&p-begin<=sizeof(Fixture)&&bytes<=sizeof(Fixture)-(p-begin);
}
NativeStereoViewInputs views(){
    NativeStereoViewInputs out;out.complete=true;out.scene=10;out.identity={1,2,3,400,5};
    out.eyes[0].owner.mask=1;out.eyes[1].owner.mask=3;
    out.eyes[1].classification.viewPosition={0,0,4};return out;
}
NativeStereoSceneInputCapture capture(Fixture& fixture,NativeStereoOwnerBias bias={true,false,0,0},NativeStereoOwnerInputLimits limits={}){
    active=&fixture;auto view=views();view.scene=reinterpret_cast<uintptr_t>(fixture.scene.data());
    return capture_native_stereo_scene_inputs(fixture.owners[0].data(),3,view,bias,bucketOrders,readable,limits);
}
void empty(const NativeStereoSceneInputCapture& p){
    assert(!p.complete&&!p.renderingEligible&&p.input.sources.empty()&&p.input.owners.owners.empty()&&p.finalizers.empty()&&!p.input.identity.valid());
}
}
int main(){
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
#endif
    NativeStereoFpGuard defaults(true);Fixture f;const auto before=f;auto captured=capture(f);
    assert(captured.complete&&!captured.renderingEligible&&captured.input.sources.size()==3&&captured.finalizers.size()==3);
    auto plan=prepare_native_stereo_scene(captured.input);
    assert(plan.complete&&plan.sourceEffectsComplete&&!plan.renderingEligible&&plan.eyes[0].size()==2&&plan.eyes[1].size()==3&&plan.eyes[1][1].source==1);
    assert(plan.eyes[0][0].squaredDistance==100&&plan.eyes[1][0].squaredDistance==36);
    assert(!std::memcmp(&f,&before,sizeof(f)));
    captured=capture(f,{true,true,2,5});assert(captured.complete&&captured.input.views.eyes[0].classification.bias==5&&captured.input.views.eyes[1].classification.bias==5);
    float bias=1;assert(native_stereo_owner_bias({true,true,-0.f,0.f},bias)&&std::signbit(bias));
    assert(!native_stereo_owner_bias({true,false,-1,0},bias));
    empty(capture(f,{false,false,0,0}));empty(capture(f,{true,true,0,std::numeric_limits<float>::infinity()}));
    // Classifier position and sorting point have independent native meanings.
    f.put(f.sources[1].data(),0x110,uint64_t(reinterpret_cast<uintptr_t>(&f.independent)));
    captured=capture(f);assert(captured.complete&&captured.input.sources[1].positionKind==NativeStereoPositionKind::IndependentPoint&&captured.input.sources[1].candidate.position.z==50&&captured.input.sources[1].sortingPosition.z==20);
    // Finalization is an actual no-op only for a null lazy pointer or bit 0x1000.
    f.put(f.sources[1].data(),0x138,uint64_t(reinterpret_cast<uintptr_t>(&f.lazyFlags[1])));
    captured=capture(f);assert(captured.complete&&captured.finalizers[1].required&&!captured.finalizers[0].required);
    assert(!prepare_native_stereo_scene(captured.input).sourceEffectsComplete);
    f.lazyFlags[1]=0x1000;captured=capture(f);assert(captured.complete&&!captured.finalizers[1].required);
    assert(prepare_native_stereo_scene(captured.input).sourceEffectsComplete);
    // Native mutation after this call cannot change copied numeric input.
    f.put(f.sources[0].data(),0xec,NativeStereoClassifyFloat3{9,9,9});
    assert(captured.input.sources[0].sortingPosition.z==10);
    f.~Fixture();new(&f)Fixture();
    f.put(f.owners[0].data(),0,uint32_t(0x400000));auto local=identity();local[12]=20;f.put(f.owners[0].data(),0x230,local);
    captured=capture(f);plan=prepare_native_stereo_scene(captured.input);
    assert(captured.complete&&plan.complete&&plan.owners.worldRefreshes==3&&plan.positionCalculations==3&&plan.eyes[0][0].squaredDistance==500);
    f.~Fixture();new(&f)Fixture();
    for(unsigned mode=0;mode<5;++mode){
        switch(mode){
        case 0:f.put(f.sources[0].data(),0x140,uint32_t(1));break;
        case 1:f.put(f.sources[0].data(),0x104,int32_t(7));break;
        case 2:f.put(f.sources[0].data(),0x110,uint64_t(0));break;
        case 3:blocked=reinterpret_cast<uintptr_t>(f.sources[0].data())+0xfc;break;
        case 4:throws=true;break;
        }
        empty(capture(f));blocked=0;throws=false;f.~Fixture();new(&f)Fixture();
    }
    NativeStereoOwnerInputLimits limits;limits.bytes=100;empty(capture(f,{true,false,0,0},limits));
    _mm_setcsr(0x1fbf);assert(capture(f).complete&&_mm_getcsr()==0x1fbf);
    allocation_refusals([&]{return capture(f);},[](const auto& p,bool complete){if(complete)assert(p.complete);else empty(p);});
    std::cout<<"Native scene input producer: real source fields, independent positions, exact root bias, lazy effects, dirty worlds, right-only inputs and atomic refusal passed\n";
}
