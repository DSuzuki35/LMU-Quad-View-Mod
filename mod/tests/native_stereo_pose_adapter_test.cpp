#define NOMINMAX
#include "../native_stereo_pose_adapter.hpp"
#include "../caller_address.hpp"
#include <cassert>
#include <iostream>

using namespace lmuqv;
namespace {
bool readable(const void* p,size_t n){
    if(!p||!n||reinterpret_cast<uintptr_t>(p)>UINTPTR_MAX-n)return false;
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(p,&info,sizeof(info))&&info.State==MEM_COMMIT&&!(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&
        reinterpret_cast<uintptr_t>(p)+n<=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
}
bool checkRegisters=false;
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
NOINLINE bool probe(){
    const auto caller=reinterpret_cast<uintptr_t>(LMU_CALLER_ADDRESS),slot=reinterpret_cast<uintptr_t>(LMU_CALLER_RETURN_SLOT);
    volatile uint64_t scratch[32]{};scratch[0]=caller;
    CONTEXT c;const bool ok=capture_native_caller_context(caller,slot,c,readable);
    assert(ok&&c.Rip==caller&&c.Rsp==slot+8&&scratch[0]==caller);
    if(checkRegisters)assert(c.Xmm6.Low==0x3f000000u&&c.Xmm7.Low==0xbe800000u&&c.Xmm8.Low==0x40000000u&&
        c.Xmm15.Low==0x3e8000003e800000ull&&c.Xmm15.High==0x3e8000003e800000ll);
    return ok;
}
void nonvolatileRegisters(){
    // Owned x64 fixture, not captured game code. Set XMM6/7/8/15 before calling
    // the compiled probe; save/restore the host's nonvolatile register values.
    // The reader stops at the probe's caller, so it never unwinds this fixture.
    std::vector<uint8_t> code{0x48,0x83,0xec,0x68,
        0x0f,0x29,0x74,0x24,0x20,0x0f,0x29,0x7c,0x24,0x30,0x44,0x0f,0x29,0x44,0x24,0x40,
        0x44,0x0f,0x29,0x7c,0x24,0x50,
        0xb8,0x00,0x00,0x00,0x3f,0x66,0x0f,0x6e,0xf0,
        0xb8,0x00,0x00,0x80,0xbe,0x66,0x0f,0x6e,0xf8,
        0xb8,0x00,0x00,0x00,0x40,0x66,0x44,0x0f,0x6e,0xc0,
        0xb8,0x00,0x00,0x80,0x3e,0x66,0x44,0x0f,0x6e,0xf8,0x66,0x45,0x0f,0x70,0xff,0x00,0x48,0xb8};
    const auto target=reinterpret_cast<uintptr_t>(&probe);
    for(unsigned i=0;i<8;++i)code.push_back(uint8_t(target>>(8*i)));
    const uint8_t tail[]{0xff,0xd0,0x0f,0x28,0x74,0x24,0x20,0x0f,0x28,0x7c,0x24,0x30,
        0x44,0x0f,0x28,0x44,0x24,0x40,0x44,0x0f,0x28,0x7c,0x24,0x50,0x48,0x83,0xc4,0x68,0xc3};
    code.insert(code.end(),std::begin(tail),std::end(tail));
    auto* memory=VirtualAlloc(nullptr,code.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(memory);
    std::memcpy(memory,code.data(),code.size());DWORD old=0;assert(VirtualProtect(memory,code.size(),PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),memory,code.size()));checkRegisters=true;
    assert(reinterpret_cast<bool(*)()>(memory)());checkRegisters=false;
    assert(VirtualFree(memory,0,MEM_RELEASE));
}
bool refuse(const void*,size_t){return false;}
}
int main(){
    assert(probe());
    nonvolatileRegisters();
    std::array<std::byte,0x700> stack{};std::array<std::byte,0x418> state{};
    const auto rsp=reinterpret_cast<uintptr_t>(stack.data());
    const std::array<float,3> origin{9,8,7};const std::array<float,9> rotation{1,2,3,4,5,6,7,8,9};
    std::memcpy(stack.data()+0x50,origin.data(),12);std::memcpy(stack.data()+0xa8,rotation.data(),16);
    // Head rotations have changed the stack copy, while translation still
    // consumes the original coefficients preserved in the native XMM15.
    const std::array<float,4> headRotated{-14,25,-36,47};
    std::memcpy(stack.data()+0x70,headRotated.data(),16);std::memcpy(stack.data()+0x660,rotation.data()+8,4);
    std::array<std::array<float,6>,2> scalars{{{1,2,3,4,5,6},{1,2,3,7,8,9}}};
    std::memcpy(state.data()+0x3e0,scalars.data(),48);
    CONTEXT c{};c.Rsp=rsp;c.Rbp=rsp+0x100;c.R14=77;c.R15=88;c.R13=1;
    std::memcpy(&c.Xmm15,rotation.data()+4,16);
    const float x=.5f,y=-.25f,z=2;std::memcpy(&c.Xmm6.Low,&x,4);std::memcpy(&c.Xmm7.Low,&y,4);std::memcpy(&c.Xmm8.Low,&z,4);
    NativeStereoWorldPoseInput out;
    assert(capture_native_stereo_pose_input(c,state.data(),77,88,readable,out));
    assert(out.origin==origin&&out.rotation==rotation&&out.scalars==scalars&&out.offset==(std::array<float,3>{x,y,z}));
    auto before=out;
    for(unsigned failure=0;failure<7;++failure){auto bad=c;
        if(failure==0)bad.Rbp+=8;else if(failure==1)bad.R14=99;else if(failure==2)bad.R15=99;
        else if(failure==3)bad.R12=1;else if(failure==4)bad.R13=0;else if(failure==5)bad.Rsp=UINTPTR_MAX-4;
        assert(!capture_native_stereo_pose_input(bad,state.data(),77,88,failure==6?refuse:readable,out));
        assert(!std::memcmp(&out,&before,sizeof(out)));
    }
    CONTEXT recovered{};assert(!capture_native_caller_context(1,UINTPTR_MAX,recovered,readable));
    std::array<std::byte,0x5d0> view{};std::array<std::byte,0xc0> manager{};std::array<std::byte,0x100> source{};
    const auto viewToken=reinterpret_cast<uintptr_t>(view.data()),managerToken=reinterpret_cast<uintptr_t>(manager.data()),sourceToken=reinterpret_cast<uintptr_t>(source.data());
    const auto put=[](void* base,size_t offset,const auto& value){std::memcpy(static_cast<std::byte*>(base)+offset,&value,sizeof(value));};
    constexpr size_t sourceOffsets[]{0x38,0x40,0x48,0x50,0,0x58,0x60,0x68,0x70};
    for(size_t offset:sourceOffsets)if(offset)put(manager.data(),offset,sourceToken);
    put(view.data(),0x4c,.25f);put(view.data(),0x50,-.5f);put(view.data(),0x10c,uint32_t(5));put(view.data(),0x3e8,int32_t(1));
    put(source.data(),0x74,200.f);put(source.data(),0x78,.125f);put(source.data(),0x7c,2.f);
    c.R14=viewToken;c.R15=managerToken;c.Rsi=sourceToken;
    NativeStereoCockpitViewSettings settings;
    for(int32_t selected=0;selected<=8;++selected){put(manager.data(),0xa0,selected);
        const bool copied=capture_native_stereo_cockpit_settings(c,viewToken,managerToken,readable,settings);
        assert(copied==(selected!=4));if(copied)assert(settings.complete&&settings.mask==5&&settings.mode==1&&
            settings.scale==2&&settings.range==199.875f&&settings.anchor.x==.25f&&settings.anchor.y==-.5f&&settings.anchor.z==99.9375f);
    }
    put(manager.data(),0xa0,int32_t(0));auto saved=settings;
    assert(!capture_native_stereo_cockpit_settings(c,viewToken,managerToken,refuse,settings));
    c.Rsi+=8;assert(!capture_native_stereo_cockpit_settings(c,viewToken,managerToken,readable,settings));c.Rsi=sourceToken;
    put(source.data(),0x78,201.f);assert(!capture_native_stereo_cockpit_settings(c,viewToken,managerToken,readable,settings));
    assert(!std::memcmp(&settings,&saved,sizeof(settings)));
    std::cout<<"native pose adapter: live Windows unwind, bounded stack/SIMD inputs and ABI refusal passed\n";
}
