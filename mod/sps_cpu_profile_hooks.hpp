#pragma once
#include "sps_cpu_profile.hpp"
#include "vendor/minhook-1.3.4/include/MinHook.h"
#include <d3d11.h>
#include <xmmintrin.h>

namespace lmuqv {
struct SpsCpuClock {
    static uint64_t now(){LARGE_INTEGER value{};QueryPerformanceCounter(&value);return uint64_t(value.QuadPart);}
    static uint64_t frequency(){LARGE_INTEGER value{};QueryPerformanceFrequency(&value);return uint64_t(value.QuadPart);}
    static unsigned thread(){return GetCurrentThreadId();}
    static bool cycles(uint64_t& value){ULONG64 result=0;const bool ok=QueryThreadCycleTime(GetCurrentThread(),&result)!=FALSE;value=result;return ok;}
};
using SpsCpuProfile=BasicSpsCpuProfile<SpsCpuClock>;
inline SpsCpuProfile spsCpuProfile;
class SpsCpuScope {
    SpsCpuProfile::Token token_;
public:
    explicit SpsCpuScope(SpsCpuWork work){const auto csr=_mm_getcsr();token_=spsCpuProfile.begin(work);_mm_setcsr(csr);}
    ~SpsCpuScope(){const auto csr=_mm_getcsr();spsCpuProfile.end(token_);_mm_setcsr(csr);}
    SpsCpuScope(const SpsCpuScope&)=delete;
    SpsCpuScope& operator=(const SpsCpuScope&)=delete;
};
namespace sps_cpu_hooks {
inline thread_local unsigned pass=4;
inline thread_local const void* expectedView=nullptr;
inline std::atomic<ID3D11DeviceContext*> context{nullptr};
inline std::array<void*,9> targets{},originals{};
inline unsigned installed=0;
inline bool matches(ID3D11DeviceContext* selected){return selected==context.load(std::memory_order_relaxed);}
template<class Fn,unsigned I> Fn original(){return reinterpret_cast<Fn>(originals[I]);}
template<class Call> void draw(ID3D11DeviceContext* c,Call call){
    if(!matches(c)){call();return;}SpsCpuScope scope(SpsCpuWork::draw);call();
}
inline void STDMETHODCALLTYPE indexed(ID3D11DeviceContext* c,UINT n,UINT start,INT base){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);draw(c,[&]{original<Fn,0>()(c,n,start,base);});}
inline void STDMETHODCALLTYPE plain(ID3D11DeviceContext* c,UINT n,UINT start){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);draw(c,[&]{original<Fn,1>()(c,n,start);});}
inline void STDMETHODCALLTYPE indexed_instanced(ID3D11DeviceContext* c,UINT n,UINT instances,UINT start,INT base,UINT first){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);draw(c,[&]{original<Fn,2>()(c,n,instances,start,base,first);});}
inline void STDMETHODCALLTYPE instanced(ID3D11DeviceContext* c,UINT n,UINT instances,UINT start,UINT first){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);draw(c,[&]{original<Fn,3>()(c,n,instances,start,first);});}
inline void STDMETHODCALLTYPE automatic(ID3D11DeviceContext* c){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);draw(c,[&]{original<Fn,4>()(c);});}
template<unsigned I> void STDMETHODCALLTYPE indirect(ID3D11DeviceContext* c,ID3D11Buffer* args,UINT offset){using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);draw(c,[&]{original<Fn,I>()(c,args,offset);});}
inline HRESULT STDMETHODCALLTYPE map(ID3D11DeviceContext* c,ID3D11Resource* resource,UINT sub,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE* result){
    using Fn=HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
    if(!matches(c))return original<Fn,7>()(c,resource,sub,type,flags,result);
    SpsCpuScope scope(SpsCpuWork::map);return original<Fn,7>()(c,resource,sub,type,flags,result);
}
inline void STDMETHODCALLTYPE unmap(ID3D11DeviceContext* c,ID3D11Resource* resource,UINT sub){
    using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT);
    if(!matches(c)){original<Fn,8>()(c,resource,sub);return;}
    SpsCpuScope scope(SpsCpuWork::unmap);original<Fn,8>()(c,resource,sub);
}
inline void stop(){
    context.store(nullptr);spsCpuProfile.stop();
    for(unsigned i=0;i<installed;++i)MH_DisableHook(targets[i]);
    for(unsigned i=0;i<installed;++i)MH_RemoveHook(targets[i]);
    installed=0;
}
// Heavy audit and this adapter are mutually exclusive; never stack detours.
inline bool configure(ID3D11DeviceContext* selected,void(*log)(const char*)){
    if(installed||!selected||selected->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
    constexpr unsigned slots[]={12,13,20,21,38,39,40,14,15};
    void* detours[]={reinterpret_cast<void*>(&indexed),reinterpret_cast<void*>(&plain),reinterpret_cast<void*>(&indexed_instanced),reinterpret_cast<void*>(&instanced),reinterpret_cast<void*>(&automatic),reinterpret_cast<void*>(&indirect<5>),reinterpret_cast<void*>(&indirect<6>),reinterpret_cast<void*>(&map),reinterpret_cast<void*>(&unmap)};
    auto** table=*reinterpret_cast<void***>(selected);
    for(unsigned i=0;i<9;++i){targets[i]=table[slots[i]];
        if(MH_CreateHook(targets[i],detours[i],&originals[i])!=MH_OK){stop();return false;}
        ++installed;
    }
    for(unsigned i=0;i<installed;++i)if(MH_EnableHook(targets[i])!=MH_OK){stop();return false;}
    context.store(selected);spsCpuProfile.configure(true,log);return true;
}
}
class SpsCpuPassScope {
    unsigned saved_;const void* savedView_;
public:
    SpsCpuPassScope(unsigned pass,const void* view):saved_(sps_cpu_hooks::pass),savedView_(sps_cpu_hooks::expectedView){sps_cpu_hooks::pass=pass;sps_cpu_hooks::expectedView=view;}
    ~SpsCpuPassScope(){sps_cpu_hooks::pass=saved_;sps_cpu_hooks::expectedView=savedView_;}
};
class SpsCpuRangeScope {
    SpsCpuProfile::RangeToken token_;
public:
    SpsCpuRangeScope(const void* view,int group,uintptr_t caller){const auto csr=_mm_getcsr();token_=spsCpuProfile.range_begin(sps_cpu_hooks::pass,view&&view==sps_cpu_hooks::expectedView&&group==31&&caller==0x51df1d);_mm_setcsr(csr);}
    ~SpsCpuRangeScope(){const auto csr=_mm_getcsr();spsCpuProfile.range_end(token_);_mm_setcsr(csr);}
};
}
