// Exercise the real render hook with a synthetic eye-loop caller and XR boundary.
// No game code, game memory or graphics device is used.
#include <cstdint>
#undef NOMINMAX
static std::uintptr_t syntheticCaller=0;
static void* testReturnAddress(unsigned){return reinterpret_cast<void*>(syntheticCaller);}
#define __builtin_return_address testReturnAddress
#include "../engine_bridge.cpp"
#undef __builtin_return_address
#include <iostream>
#include <cstdlib>

namespace lmuqv {
unsigned originalCalls=0,viewRequests=0;
bool xr_runtime_sizes(unsigned,uint32_t&,uint32_t&,uint32_t&,uint32_t&){return false;}
bool xr_quad_active(){return true;}
bool xr_frame_views(XrView (&)[4]){++viewRequests;return false;}
bool xr_copy_outer(unsigned,ID3D11Texture2D*){return false;}
void xr_scene_focus_complete(unsigned){}
ID3D11RenderTargetView* xr_outer_rtv(unsigned){return nullptr;}
void log_line(const char*){}
}
static void original(void*,bool){++lmuqv::originalCalls;}
static void require(bool condition,const char* message){
    if(!condition){std::cerr<<message<<'\n';std::exit(1);}
}
int main(){
    using namespace lmuqv;
    base=0x10000000;syntheticCaller=base+0xcd1763;
    installed.store(true);renderOriginal=original;
    nativeRuntimeSettings=true;nativeOuterOptIn=true;nativeOuterFailed=true;
    // Resource-bank creation failed before binding: the original renderer is intact.
    renderHook(nullptr,false);
    require(originalCalls==1,"fallback skipped original rendering");
    require(viewRequests==1,"recoverable native-bank failure suppresses quad scene submission");
    // Failure persists for the session, but live scene frames must keep flowing.
    renderHook(nullptr,false);
    require(originalCalls==2&&viewRequests==2,"fallback stopped on the following frame");
    syntheticCaller=base+0x1234;
    renderHook(nullptr,false);
    require(originalCalls==3&&viewRequests==2,"fallback accepted an unverified eye-loop caller");
    syntheticCaller=base+0xcd1763;
    nativeQuadSubmissionBlocked=true;
    renderHook(nullptr,false);
    require(originalCalls==4&&viewRequests==2,"unsafe dimensions/restoration failure allowed quad submission");
    nativeRuntimeSettings=false;
    renderHook(nullptr,false);
    require(originalCalls==5&&viewRequests==2,"legacy mode bypassed unsafe restoration guard");
    installed.store(false);
    std::cout<<"Native outer fallback: live scene continuation and caller guard passed\n";
}
