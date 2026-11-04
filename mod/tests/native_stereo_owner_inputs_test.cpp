#include "../native_stereo_owner_inputs.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>

namespace failure_probe {
bool enabled=false;size_t count=0,failAt=0;
}
void* operator new(size_t bytes){
    if(failure_probe::enabled&&++failure_probe::count==failure_probe::failAt)throw std::bad_alloc();
    if(void* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();
}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}

using namespace lmuqv;
namespace {
struct Fixture {
    std::array<std::array<unsigned char,0x430>,3> owner{};
    std::array<std::array<unsigned char,0x180>,3> source{};
    std::array<std::array<unsigned char,0x1c0>,3> mesh{};
    std::array<uint64_t,2> children{};
    std::array<std::array<uint64_t,1>,3> sourcePointers{};
    template<class T>static void put(void* base,size_t offset,const T& value){std::memcpy(static_cast<unsigned char*>(base)+offset,&value,sizeof(T));}
    static void vector(void* base,size_t offset,uint64_t begin,size_t count){
        put(base,offset,begin);put(base,offset+8,begin+8*count);put(base,offset+16,begin+8*count);
    }
    Fixture(){
        for(unsigned i=0;i<3;++i){
            const uint32_t flags=0,mask=i==1?2:0;const uint64_t pending=100+i,current=40+i;
            put(owner[i].data(),0,flags);put(owner[i].data(),0x74,mask);put(owner[i].data(),0x3d8,pending);put(owner[i].data(),0x3e0,current);
            const NativeStereoMatrix world{1,0,0,0,0,1,0,0,0,0,1,0,float(i),0,0,1};put(owner[i].data(),0x298,world);
            sourcePointers[i][0]=reinterpret_cast<uintptr_t>(source[i].data());
            vector(owner[i].data(),0x88,reinterpret_cast<uintptr_t>(sourcePointers[i].data()),1);
            put(source[i].data(),0x130,uint64_t(reinterpret_cast<uintptr_t>(owner[i].data())));
            put(source[i].data(),0x168,int32_t(2));put(source[i].data(),0x170,uint64_t(reinterpret_cast<uintptr_t>(mesh[i].data())));
            for(unsigned m=0;m<2;++m){
                put(mesh[i].data()+0xe0*m,0,uint32_t(0x8000));
                put(mesh[i].data()+0xe0*m,0x80,sourcePointers[i][0]);
            }
        }
        children={reinterpret_cast<uintptr_t>(owner[1].data()),reinterpret_cast<uintptr_t>(owner[2].data())};
        vector(owner[0].data(),0xa0,reinterpret_cast<uintptr_t>(children.data()),2);
    }
};
Fixture* fixture=nullptr;size_t reads=0;uintptr_t blocked=0;bool throwRead=false;
std::array<std::pair<uintptr_t,size_t>,2> deepSpans{};
bool deepReadable(const void* address,size_t bytes){
    const auto p=reinterpret_cast<uintptr_t>(address);
    for(const auto& span:deepSpans)if(p>=span.first&&p-span.first<=span.second&&bytes<=span.second-(p-span.first))return true;
    return false;
}
bool readable(const void* address,size_t bytes){
    ++reads;if(throwRead)throw 1;
    const auto begin=reinterpret_cast<uintptr_t>(fixture),p=reinterpret_cast<uintptr_t>(address);
    return p!=blocked&&p>=begin&&p-begin<=sizeof(Fixture)&&bytes<=sizeof(Fixture)-(p-begin);
}
NativeStereoOwnerInputs capture(Fixture& f,NativeStereoOwnerInputLimits limits={}){
    fixture=&f;return capture_native_stereo_owner_inputs(f.owner[0].data(),3,readable,limits);
}
void empty(const NativeStereoOwnerInputs& out,const char* reason){
    assert(!out.complete&&!out.renderingEligible&&out.owners.empty()&&out.world.empty()&&out.ownerTokens.empty()&&out.sources.empty()&&out.meshes.empty());
    assert(std::string(out.reason).find(reason)!=std::string::npos);
}
}
int main(){
    Fixture f;const auto original=f;_mm_setcsr(0x1fbf);auto out=capture(f);
    assert(out.complete&&!out.renderingEligible&&out.owners.size()==3&&out.sources.size()==3&&out.meshes.size()==6);
    assert(_mm_getcsr()==0x1fbf&&!std::memcmp(&f,&original,sizeof(f)));
    assert(out.owners[0].children==std::vector<uint32_t>({1,2}));
    assert(out.sources[1].owner==1&&out.sources[1].firstMesh==2&&out.meshes[3].source==1&&out.meshes[3].index==1);
    auto plan=plan_native_stereo_owner_inputs(out,{{{1,true},{3,true}}});
    assert(plan.complete&&!plan.renderingEligible);
    assert(plan.eyes[0].ownerOrder==std::vector<uint32_t>({0,2}));
    assert(plan.eyes[1].ownerOrder==std::vector<uint32_t>({0,1,2}));
    assert(plan.eyes[1].sourceOrder==std::vector<uint32_t>({0,1,2}));
    // Both a left-rejected root and its right-only subtree are copied intact.
    Fixture::put(f.owner[0].data(),0x74,uint32_t(2));out=capture(f);
    plan=plan_native_stereo_owner_inputs(out,{{{1,true},{3,true}}});
    assert(out.complete&&plan.complete&&plan.eyes[0].ownerOrder.empty()&&plan.eyes[1].ownerOrder.size()==3);
    // Source descriptions survive native changes; saved tokens are never read.
    Fixture::put(f.mesh[1].data(),0,uint32_t(77));Fixture::put(f.owner[1].data(),0x3e0,uint64_t(900));
    assert(out.meshes[2].flags==0x8000&&out.owners[1].state.currentValue==41);
    f.~Fixture();new(&f)Fixture();
    Fixture::put(f.owner[1].data(),0,uint32_t(0x400000));empty(capture(f),"refresh");
    Fixture::put(f.owner[1].data(),0x80,uint64_t(1));assert(capture(f).complete); // native attachment bypass
    Fixture::put(f.owner[1].data(),0,uint32_t(0));Fixture::put(f.owner[1].data(),0x80,uint64_t(0));
    f.children[1]=f.children[0];empty(capture(f),"not a tree");f.children[1]=reinterpret_cast<uintptr_t>(f.owner[2].data());
    f.children[1]=reinterpret_cast<uintptr_t>(f.owner[0].data());empty(capture(f),"not a tree");f.children[1]=reinterpret_cast<uintptr_t>(f.owner[2].data());
    Fixture::put(f.owner[1].data(),0xa0,uint64_t(1));empty(capture(f),"malformed");Fixture::vector(f.owner[1].data(),0xa0,0,0);
    Fixture::put(f.source[1].data(),0x130,uint64_t(1));empty(capture(f),"different native owner");
    Fixture::put(f.source[1].data(),0x130,uint64_t(reinterpret_cast<uintptr_t>(f.owner[1].data())));
    Fixture::put(f.source[1].data(),0x168,int32_t(-1));empty(capture(f),"mesh input limit");Fixture::put(f.source[1].data(),0x168,int32_t(2));
    Fixture::put(f.source[1].data(),0x170,uint64_t(-16));empty(capture(f),"source array");
    Fixture::put(f.source[1].data(),0x170,uint64_t(reinterpret_cast<uintptr_t>(f.mesh[1].data())));
    blocked=reinterpret_cast<uintptr_t>(f.owner[2].data());empty(capture(f),"unreadable");blocked=0;
    throwRead=true;empty(capture(f),"reader failure");throwRead=false;
    NativeStereoOwnerInputLimits limits;limits.owners=2;empty(capture(f,limits),"vector limit");
    limits={};limits.sources=2;empty(capture(f,limits),"vector limit");
    limits={};limits.meshes=5;empty(capture(f,limits),"mesh input limit");
    limits={};limits.bytes=1;reads=0;empty(capture(f,limits),"byte limit");assert(reads==0);
    limits={};limits.owners=0;empty(capture(f,limits),"limits");
    reads=0;empty(capture_native_stereo_owner_inputs(reinterpret_cast<const void*>((std::numeric_limits<uintptr_t>::max)()-1),3,readable),"address overflow");assert(reads==0);
    failure_probe::enabled=true;failure_probe::count=0;failure_probe::failAt=size_t(-1);out=capture(f);
    failure_probe::enabled=false;assert(out.complete);const auto allocations=failure_probe::count;
    for(size_t at=1;at<=allocations;++at){failure_probe::enabled=true;failure_probe::count=0;failure_probe::failAt=at;
        out=capture(f);failure_probe::enabled=false;empty(out,"allocation failure");}
    // Frame publication requires a successful seal; never expose a partial forest.
    NativeStereoOwnerInputFrame frame;const StereoFrameIdentity key{1,2,3,4000,5};
    frame.begin(key,10,20,{1,true});assert(!frame.inputs(key));fixture=&f;
    assert(frame.append(key,10,20,{1,true},f.owner[0].data(),3,readable));assert(!frame.inputs(key));
    assert(frame.seal(key)&&frame.inputs(key)->size()==1);
    assert(!frame.append(key,10,20,{1,true},f.owner[0].data(),3,readable));
    frame.begin(key,10,20,{1,true});assert(frame.append(key,10,20,{1,true},f.owner[0].data(),3,readable));
    assert(!frame.append(key,10,20,{1,true},f.owner[0].data(),3,readable)&&!frame.inputs(key));
    frame.begin(key,10,20,{1,true});assert(!frame.append(key,10,20,{2,true},f.owner[0].data(),3,readable));
    frame.begin(key,10,20,{1,true});auto next=key;++next.frame;
    assert(!frame.append(next,10,20,{1,true},f.owner[0].data(),3,readable)&&!frame.seal(key));
    frame.reset();assert(!frame.inputs(key));
    // Maximum-depth native trees must not consume the C++ call stack.
    std::vector<std::array<unsigned char,0x430>> deep(nativeStereoOwnerNodeLimit);
    std::vector<uint64_t> links(nativeStereoOwnerNodeLimit-1);
    for(size_t i=0;i+1<deep.size();++i){links[i]=reinterpret_cast<uintptr_t>(deep[i+1].data());
        Fixture::vector(deep[i].data(),0xa0,reinterpret_cast<uintptr_t>(&links[i]),1);}
    deepSpans={{{reinterpret_cast<uintptr_t>(deep.data()),deep.size()*sizeof(deep[0])},
                {reinterpret_cast<uintptr_t>(links.data()),links.size()*sizeof(links[0])}}};
    auto maximum=capture_native_stereo_owner_inputs(deep[0].data(),3,deepReadable);
    assert(maximum.complete&&maximum.owners.size()==nativeStereoOwnerNodeLimit&&maximum.sources.empty());
    limits={};--limits.owners;
    empty(capture_native_stereo_owner_inputs(deep[0].data(),3,deepReadable,limits),"vector limit");
    std::cout<<"Owned native source producer: unfiltered/right-only trees, mesh descriptions, no native mutation, strict bounds, allocation failures, frame sealing and admission planner passed\n";
}
