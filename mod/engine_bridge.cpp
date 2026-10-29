#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "bridge.hpp"
#include "quad_math.hpp"
#include "ui_d3d_state.hpp"
#include "gpu_timing.hpp"
#include "pipeline_stats.hpp"
#include "benchmark_control.hpp"
#include "code_anchors.hpp"
#include "nv_vrs.hpp"
#include "vrs_scope_policy.hpp"
#include "native_outer_bank.hpp"
#include "native_outer_anchors.hpp"
#include "xr_sizes.hpp"
#include "../prototype/lmu_reconstructed.hpp"
#include "vendor/minhook-1.3.4/include/MinHook.h"
#include <wincrypt.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace lmuqv {
namespace {
using lmu::reconstructed::TransformRecord;
using lmu::reconstructed::ViewSnapshot;
using RenderFn = void (*)(void*, bool);
using SceneFn = void (*)(void*, bool, bool);
using SetProjectionFn = void (*)(void*, const float*);
using DerivedFn = void (*)(void*, unsigned, bool);
using MaskFn = void (*)(void*, void*);
using MainPassFn = void (*)(void*, void*, void*, std::uint32_t, bool);
using WorldSortFn = void (*)(void*, void*, void*, std::int32_t, std::uint32_t, bool, void*);
using NativeViewFn = void (*)(void*,void*,void*,bool,bool);

#pragma pack(push,1)
struct UiArgs { std::uint32_t mode; TransformRecord transform[2]; };
#pragma pack(pop)
static_assert(sizeof(UiArgs)==0xdc);
using UiFn=void (*)(void*,ID3D11DeviceContext*,const UiArgs*);
using UiEyeFn=void (*)(void*,ID3D11DeviceContext*,const UiArgs*,unsigned);
std::uintptr_t base{};
RenderFn renderOriginal{};
UiFn uiOriginal{};
SetProjectionFn projectionOriginal{};
DerivedFn derivedOriginal{};
MaskFn maskOriginal{};
MainPassFn mainPassOriginal{};
WorldSortFn worldSortOriginal{};
NativeViewFn nativeViewOriginal{};
NativeOuterBank nativeOuterBanks[2];
bool nativeOuterOptIn=false,nativeOuterFailed=false;
// Failure to allocate an optional small bank is recoverable. A size mismatch
// or failed restoration is not: keep those separate from the performance path.
bool nativeQuadSubmissionBlocked=false;
XrOutputSizes nativeOuterSizes;
bool nativeRuntimeSettings=false,nativeOuterSeparateBanks=false;
bool nativeOuterDimensions(unsigned eye,unsigned fullW,unsigned fullH,unsigned& width,unsigned& height){
    if(nativeRuntimeSettings){
        uint32_t focusW=0,focusH=0;
        if(!xr_runtime_sizes(eye,width,height,focusW,focusH)||fullW!=focusW||fullH!=focusH)return false;
    }else{width=outer_dimension(fullW,nativeOuterSizes);height=outer_dimension(fullH,nativeOuterSizes);}
    return width&&height&&width<=fullW&&height<=fullH;
}
thread_local ViewSnapshot* nativeOuterExpected=nullptr;
thread_local ViewSnapshot* nativeOuterActive=nullptr;
thread_local const Matrix* nativeOuterProjection=nullptr;
thread_local ID3D11Texture2D* nativeOuterSource=nullptr;
thread_local NativeOuterBindings* nativeOuterBindings=nullptr;
std::uint64_t nativeOuterCompleted[2]{};
std::uint64_t nativeOuterFull[2]{};
unsigned nativeBindingProbes[2]{};
ViewSnapshot* nativeOuterWarmupView=nullptr;
unsigned nativeOuterWarmupCalls=0;
std::atomic<bool> installed{false};
std::size_t installedHookCount=0;
GpuTiming gpuTiming;
PipelineStats pipelineStats;
BenchmarkControl benchmarkControl;
std::wstring benchmarkPath;
struct BenchmarkSnapshot {uint64_t phase=0;unsigned requestedRate=1;};
BenchmarkSnapshot benchmarkSnapshot;
NvVrs worldVrs;
bool worldVrsOptIn=false, worldVrsFailed=false;
ID3D11Device* worldVrsDevice=nullptr;
ID3D11DeviceContext* worldVrsConfiguredContext=nullptr;
unsigned worldVrsWidth=0, worldVrsHeight=0, worldVrsRate=0;
unsigned worldVrsDefaultRate=1;
struct VrsCounters {
    std::uint64_t attempted[2]{}, applied[2]{}, failures=0, interrupted=0;
};
VrsCounters worldVrsCounters;
std::wstring timingTrigger;
bool timingArmed=false;
unsigned timingMask=0xf;
std::atomic<uint64_t> renderCalls{0},focusCalls{0};
std::atomic<uint64_t> uiCalls{0};
struct History { ViewSnapshot* owner{}; TransformRecord transform{}; bool valid=false; };
History innerHistory[2];
void prepareHistory(History& history,ViewSnapshot* owner,bool nativePreviousValid) {
    // DYN invalidates native history on camera-definition changes even when View* is unchanged.
    if(history.owner!=owner || !nativePreviousValid) history=History{owner,{},false};
}
thread_local bool insideHook=false;
thread_local ViewSnapshot* focusView=nullptr;
thread_local const Matrix* focusProjection=nullptr;
thread_local unsigned projectionOverrides=0;
thread_local unsigned skippedMasks=0;
thread_local std::array<std::uintptr_t,16> seenProjectionCallers{};
thread_local unsigned seenProjectionCount=0;
thread_local ViewSnapshot* selectedOuterView=nullptr;
thread_local ID3D11DeviceContext* outerVrsContext=nullptr;
thread_local bool outerMainPassMarker=false;
thread_local unsigned outerSortDepth=0;
thread_local std::uint64_t vrsInterruptionGeneration=0;

void maskHook(void* mask, void* view) {
    // The upstream main-pass color clear still runs. Only the wide-FOV mask draw is skipped.
    if(view==focusView && focusProjection) {++skippedMasks;return;}
    maskOriginal(mask,view);
}

void projectionHook(void* view, const float* value) {
    Matrix modified{};
    if(view==nativeOuterActive && nativeOuterProjection){
        std::memcpy(modified.data(),value,sizeof(modified));
        modified=replace_fov(modified,*nativeOuterProjection);value=modified.data();
    }
    if(view==focusView && focusProjection) {
        const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        if(caller>=base && caller-base<0x4000000 && seenProjectionCount<seenProjectionCallers.size()) {
            bool seen=false;
            for(unsigned i=0;i<seenProjectionCount;++i)seen|=seenProjectionCallers[i]==caller;
            if(!seen) {
                seenProjectionCallers[seenProjectionCount++]=caller;
                char message[240];std::snprintf(message,sizeof(message),
                    "engine focus projection callerRVA=%llx input00=%.6f input10=%.9f input14=%.9f flags=%x",
                    static_cast<unsigned long long>(caller-base),value[0],value[10],value[14],focusView->flags);
                log_line(message);
            }
        }
        std::memcpy(modified.data(),value,sizeof(modified));
        modified=replace_fov(modified,*focusProjection);
        value=modified.data();
        ++projectionOverrides;
    }
    projectionOriginal(view,value);
}
void derivedHook(void* pointer, unsigned flags, bool suppressRendererUpdate) {
    if(pointer==nativeOuterActive&&nativeOuterProjection){
        nativeOuterActive->flags=(nativeOuterActive->flags&~0x200000u)|0x100000u;
        nativeOuterActive->projection=replace_fov(nativeOuterActive->projection,*nativeOuterProjection);
    }
    if(pointer==focusView && focusProjection) {
        // 557988 otherwise regenerates a matrix before checking the explicit-matrix flag.
        focusView->flags=(focusView->flags & ~0x200000u) | 0x100000u;
        focusView->projection=replace_fov(focusView->projection,*focusProjection);
    }
    derivedOriginal(pointer,flags,suppressRendererUpdate);
}

template<class T> T* address(std::uintptr_t rva) { return reinterpret_cast<T*>(base+rva); }
bool readable(const void* pointer, std::size_t bytes) {
    if(!pointer || !bytes) return false;
    auto current=reinterpret_cast<std::uintptr_t>(pointer);
    if(current+bytes<current) return false;
    const auto end=current+bytes;
    while(current<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(current),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        auto next=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
        if(next<=current) return false;
        current=next;
    }
    return true;
}
bool correctDiskBuild() {
    wchar_t path[32768]{};
    if(!GetModuleFileNameW(nullptr,path,32768) ||
       _wcsicmp(std::filesystem::path(path).filename().c_str(),L"Le Mans Ultimate.exe")) return false;
    std::ifstream file(std::filesystem::path(path),std::ios::binary);
    if(!file) return false;
    HCRYPTPROV provider{}; HCRYPTHASH hash{};
    if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)) return false;
    if(!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)) {CryptReleaseContext(provider,0);return false;}
    bool okay=true;
    char bytes[65536];
    while(file) { file.read(bytes,sizeof(bytes)); auto n=file.gcount();
        if(n && !CryptHashData(hash,reinterpret_cast<BYTE*>(bytes),DWORD(n),0)) {okay=false;break;} }
    if(!file.eof()) okay=false;
    BYTE digest[32]{}; DWORD size=sizeof(digest);
    okay=okay && CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0) && size==32;
    CryptDestroyHash(hash);CryptReleaseContext(provider,0);
    char hex[65]{}; for(unsigned i=0;i<32;++i) std::snprintf(hex+2*i,3,"%02x",digest[i]);
    return okay && !std::strcmp(hex,lmu::reconstructed::executable_sha256);
}
template<std::size_t N> bool matches(std::uintptr_t rva,const std::array<std::uint8_t,N>& bytes) {
    return readable(address<void>(rva),N) && !std::memcmp(address<void>(rva),bytes.data(),N);
}
Fov fov(const XrView& v) { return {v.fov.angleLeft,v.fov.angleRight,v.fov.angleUp,v.fov.angleDown}; }
bool projection(const XrView& v,float nearZ,float farZ,Matrix& result) {
    auto renderer=*address<std::uintptr_t>(0x1b12be8);
    if(!readable(reinterpret_cast<void*>(renderer+0x770e0),8)) return false;
    float sx{},sy{};std::memcpy(&sx,reinterpret_cast<void*>(renderer+0x770e0),4);
    std::memcpy(&sy,reinterpret_cast<void*>(renderer+0x770e4),4);
    return make_engine_projection(fov(v),nearZ,farZ,sx,sy,result);
}
bool equalPose(const XrPosef& a,const XrPosef& b) {
    const float sign=a.orientation.x*b.orientation.x+a.orientation.y*b.orientation.y+
        a.orientation.z*b.orientation.z+a.orientation.w*b.orientation.w < 0 ? -1.f : 1.f;
    return std::abs(a.position.x-b.position.x)<1e-5f && std::abs(a.position.y-b.position.y)<1e-5f &&
        std::abs(a.position.z-b.position.z)<1e-5f && std::abs(a.orientation.x-sign*b.orientation.x)<1e-5f &&
        std::abs(a.orientation.y-sign*b.orientation.y)<1e-5f && std::abs(a.orientation.z-sign*b.orientation.z)<1e-5f &&
        std::abs(a.orientation.w-sign*b.orientation.w)<1e-5f;
}
bool timingReady() {
    if(timingTrigger.empty() || gpuTiming.done() || gpuTiming.faulted()) return false;
    if(!timingArmed) {
        const auto attributes=GetFileAttributesW(timingTrigger.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY)) return false;
        gpuTiming.configure(true,timingMask,[](void*,const char* message){log_line(message);});
        timingArmed=true;
        log_line("engine GPU timing: trigger observed; outer includes native preparation, focus is SceneRender only; excludes UI/XR copies");
    }
    return true;
}
// This is a request, not evidence that a shading rate was applied by the driver.
unsigned benchmarkRequestedRate() {return benchmarkPath.empty()?worldVrsDefaultRate:benchmarkControl.requestedRate();}
bool parseVrsDefaultRate(bool optIn,const char* value,std::size_t length,unsigned& result) {
    result=optIn?2:1;
    if(!optIn || length==0)return true;
    if(length!=1 || !value || (value[0]!='1' && value[0]!='2' && value[0]!='4'))return false;
    result=unsigned(value[0]-'0');return true;
}
void vrsLog(void*,const char* message) {
    char line[1400]{};
    std::snprintf(line,sizeof(line),"world_vrs phase=%llu requested_rate=%u %s",
        static_cast<unsigned long long>(benchmarkSnapshot.phase),benchmarkRequestedRate(),message);
    log_line(line);
}
void resetWorldVrs() {
    worldVrs.reset();
    if(worldVrs.faulted()) {
        worldVrsFailed=true;
        vrsLog(nullptr,"reset_failure; feature disabled; known-disabled restoration not confirmed");
    }
    if(worldVrsDevice)worldVrsDevice->Release();
    worldVrsDevice=nullptr;worldVrsConfiguredContext=nullptr;
    worldVrsWidth=0;worldVrsHeight=0;worldVrsRate=0;
}
void failWorldVrs(const char* operation) {
    ++worldVrsCounters.failures;
    worldVrsFailed=true;
    vrsLog(nullptr,operation);
    resetWorldVrs();
}
bool prepareWorldVrs(ID3D11DeviceContext* context,ViewSnapshot* view) {
    if(!worldVrsOptIn || worldVrsFailed || !context || !view || view->eye_index!=0 ||
       !xr_quad_active())return false;
    auto* texture=*address<ID3D11Texture2D*>(0x1b1c6b8);
    if(!readable(texture,sizeof(void*)))return false;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(!desc.Width || !desc.Height)return false;
    ID3D11Device* device=nullptr;context->GetDevice(&device);
    if(!device) {failWorldVrs("configure_failure: no device");return false;}
    const unsigned rate=benchmarkRequestedRate();
    const bool unchanged=worldVrs.ready() && worldVrsDevice==device &&
        worldVrsConfiguredContext==context && worldVrsWidth==desc.Width &&
        worldVrsHeight==desc.Height && worldVrsRate==rate;
    if(unchanged) {device->Release();return true;}
    resetWorldVrs();
    if(worldVrsFailed) {device->Release();return false;}
    const bool okay=worldVrs.configure(context,desc.Width,desc.Height,rate,true,
        NvVrsOwnership::ExclusiveKnownDisabled,vrsLog,nullptr);
    if(!okay || !worldVrs.ready() || worldVrs.faulted()) {
        device->Release();failWorldVrs("configure_failure; feature disabled");return false;
    }
    worldVrsDevice=device;worldVrsConfiguredContext=context;
    worldVrsWidth=desc.Width;worldVrsHeight=desc.Height;worldVrsRate=rate;
    char line[200]{};
    std::snprintf(line,sizeof(line),"configured size=%ux%u rate=%u ownership=ExclusiveKnownDisabled; only verified scene sort scopes; other coverage unqualified",
        desc.Width,desc.Height,rate);vrsLog(nullptr,line);
    return true;
}
void logVrsCounters() {
    char line[550]{};
    std::snprintf(line,sizeof(line),
        "counters opt_in=%u configured_rate=%u ready=%u faulted=%u appliedScopeCounter0=%llu appliedScopeCounter1=%llu attempted0=%llu attempted1=%llu failures=%llu interrupted=%llu; successful begin+end only; rate1 is a 1x1 baseline",
        unsigned(worldVrsOptIn),worldVrsRate,unsigned(worldVrs.ready()),unsigned(worldVrsFailed||worldVrs.faulted()),
        static_cast<unsigned long long>(worldVrsCounters.applied[0]),
        static_cast<unsigned long long>(worldVrsCounters.applied[1]),
        static_cast<unsigned long long>(worldVrsCounters.attempted[0]),
        static_cast<unsigned long long>(worldVrsCounters.attempted[1]),
        static_cast<unsigned long long>(worldVrsCounters.failures),
        static_cast<unsigned long long>(worldVrsCounters.interrupted));
    vrsLog(nullptr,line);
}
void benchmarkLog(void* user,const char* message) {
    const auto& snapshot=*static_cast<const BenchmarkSnapshot*>(user);
    char line[1024]{};
    std::snprintf(line,sizeof(line),"bench phase=%llu requested_rate=%u %s",
        static_cast<unsigned long long>(snapshot.phase),snapshot.requestedRate,message);
    log_line(line);
}
bool readBenchmarkCommand(std::string& command) {
    if(benchmarkPath.empty())return false;
    const HANDLE file=CreateFileW(benchmarkPath.c_str(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};char bytes[257]{};DWORD count=0;
    const bool okay=GetFileType(file)==FILE_TYPE_DISK&&GetFileSizeEx(file,&size)&&
        size.QuadPart>0&&size.QuadPart<=256&&ReadFile(file,bytes,sizeof(bytes),&count,nullptr)&&
        count>0&&count<=256&&count==static_cast<DWORD>(size.QuadPart);
    CloseHandle(file);
    if(!okay)return false;
    command.assign(bytes,count);return true;
}
void benchmarkFrame(ID3D11DeviceContext* context=nullptr,ViewSnapshot* view=nullptr) {
    std::string command;
    if(readBenchmarkCommand(command)&&benchmarkControl.beginCommand(command)) {
        // Clear callbacks/queries before replacing the stable numeric snapshot.
        gpuTiming.reset();pipelineStats.reset();
        benchmarkSnapshot={benchmarkControl.phaseId(),benchmarkControl.requestedRate()};
        worldVrsCounters={};
        char line[200]{};std::snprintf(line,sizeof(line),
            "bench_command phase=%llu requested_rate=%u warmup_frames=%u",
            static_cast<unsigned long long>(benchmarkSnapshot.phase),benchmarkSnapshot.requestedRate,
            benchmarkControl.warmupRemaining());log_line(line);
    }
    if(context && view && !nativeOuterOptIn)prepareWorldVrs(context,view);
    if(benchmarkControl.onFrame()) {
        // Counters now describe the measurement interval, excluding its warmup.
        worldVrsCounters={};
        nativeOuterCompleted[0]=nativeOuterCompleted[1]=0;
        nativeOuterFull[0]=nativeOuterFull[1]=0;
        if(nativeOuterOptIn && benchmarkRequestedRate()!=1 && benchmarkRequestedRate()!=2){
            benchmarkLog(&benchmarkSnapshot,"bench_failed: native resolution requires mode 1=full or 2=small");
            benchmarkControl.complete();return;
        }
        gpuTiming.configure(true,0xf,benchmarkLog,&benchmarkSnapshot);
        pipelineStats.configure(true,0xf,benchmarkLog,&benchmarkSnapshot);
        benchmarkLog(&benchmarkSnapshot,"bench_start outer includes native preparation; focus is SceneRender only; excludes UI/XR copies");
    }
}
bool benchmarkSlotComplete(const GpuTimingStats& gpu,const PipelineStatsResult& work) {
    // Benchmark contract: both bounded profilers must produce all 60 valid samples.
    return gpu.done && work.done && gpu.samples==60 && work.samples==60;
}
bool nativeBenchmarkComplete(){
    const auto rate=benchmarkRequestedRate();
    if(nativeOuterFailed || (rate!=1 && rate!=2))return false;
    const auto* active=rate==1?nativeOuterFull:nativeOuterCompleted;
    const auto* inactive=rate==1?nativeOuterCompleted:nativeOuterFull;
    return active[0]>=60 && active[1]>=60 && inactive[0]==0 && inactive[1]==0;
}
void benchmarkPoll(ID3D11DeviceContext* context) {
    if(benchmarkControl.state()!=BenchmarkControl::State::measuring)return;
    gpuTiming.poll(context);pipelineStats.poll(context);
    const bool failed=gpuTiming.faulted()||pipelineStats.faulted();
    if(!failed&&!(gpuTiming.done()&&pipelineStats.done()))return;
    bool allSlotsComplete=true;
    for(unsigned slot=0;slot<4;++slot) {
        const auto gpu=gpuTiming.stats(slot);const auto work=pipelineStats.stats(slot);
        allSlotsComplete=allSlotsComplete && benchmarkSlotComplete(gpu,work);
        char line[800]{};std::snprintf(line,sizeof(line),
            "bench_counters slot=%u gpu_samples=%u gpu_issued=%llu gpu_disjoint=%llu gpu_dropped=%llu gpu_failures=%llu pipeline_samples=%u pipeline_issued=%llu pipeline_dropped=%llu pipeline_failures=%llu PSInvocations_sum=%llu VSInvocations_sum=%llu IAPrimitives_sum=%llu CSInvocations_sum=%llu",
            slot,gpu.samples,static_cast<unsigned long long>(gpu.issued),static_cast<unsigned long long>(gpu.disjoint),
            static_cast<unsigned long long>(gpu.dropped),static_cast<unsigned long long>(gpu.failures),
            work.samples,static_cast<unsigned long long>(work.issued),static_cast<unsigned long long>(work.dropped),
            static_cast<unsigned long long>(work.failures),static_cast<unsigned long long>(work.ps_sum),
            static_cast<unsigned long long>(work.vs_sum),static_cast<unsigned long long>(work.ia_sum),
            static_cast<unsigned long long>(work.cs_sum));benchmarkLog(&benchmarkSnapshot,line);
    }
    logVrsCounters();
    const bool vrsExpected=!nativeOuterOptIn && (worldVrsOptIn || benchmarkRequestedRate()!=1);
    const bool vrsIncomplete=vrsExpected && (worldVrsFailed || !worldVrs.ready() ||
        worldVrsRate!=benchmarkRequestedRate() || worldVrsCounters.applied[0]==0 ||
        worldVrsCounters.applied[1]==0 || worldVrsCounters.failures!=0 ||
        worldVrsCounters.interrupted!=0);
    if(!allSlotsComplete)benchmarkLog(&benchmarkSnapshot,"bench_incomplete: every active slot requires 60 valid GPU and pipeline samples");
    if(nativeOuterOptIn){
        char line[320]{};std::snprintf(line,sizeof(line),
            "native_resolution phase=%llu requested_mode=%s full0=%llu full1=%llu small0=%llu small1=%llu failed=%u",
            static_cast<unsigned long long>(benchmarkSnapshot.phase),benchmarkRequestedRate()==1?"full":"small",
            static_cast<unsigned long long>(nativeOuterFull[0]),static_cast<unsigned long long>(nativeOuterFull[1]),
            static_cast<unsigned long long>(nativeOuterCompleted[0]),static_cast<unsigned long long>(nativeOuterCompleted[1]),unsigned(nativeOuterFailed));
        log_line(line);
    }
    benchmarkLog(&benchmarkSnapshot,(failed||vrsIncomplete||!allSlotsComplete||(nativeOuterOptIn&&!nativeBenchmarkComplete()))?"bench_failed":"bench_complete");
    benchmarkControl.complete();
    if(failed){gpuTiming.reset();pipelineStats.reset();}
}
vrs_scope::ScopeInput vrsInput(void* pointer,bool nestedExcluded=false) {
    auto* view=static_cast<ViewSnapshot*>(pointer);
    return {selectedOuterView!=nullptr,insideHook || focusView!=nullptr,nestedExcluded,
        reinterpret_cast<std::uintptr_t>(selectedOuterView),reinterpret_cast<std::uintptr_t>(view),
        readable(view,sizeof(*view))?view->flags:0xc0000000u};
}
void suspendUnexpectedVrs() {
    if(!worldVrs.active())return;
    ++vrsInterruptionGeneration;
    ++worldVrsCounters.interrupted;
    if(!worldVrs.end_outer(outerVrsContext))
        failWorldVrs("nested_scope_disable_failure; feature disabled");
}
void mainPassHook(void* renderer,void* scene,void* view,std::uint32_t flags,bool lastMainPass) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    const auto callerRva=caller>=base?caller-base:0;
    const bool saved=outerMainPassMarker;
    // A rejected nested invocation must not inherit eligibility from its parent.
    outerMainPassMarker=worldVrsOptIn && !worldVrsFailed &&
        vrs_scope::outer_main_pass_eligible(vrsInput(view,outerSortDepth!=0),callerRva);
    suspendUnexpectedVrs();
    mainPassOriginal(renderer,scene,view,flags,lastMainPass);
    outerMainPassMarker=saved;
}
void worldSortHook(void* renderer,void* scene,void* view,std::int32_t group,
                   std::uint32_t flags,bool lastMainPass,void* optionalMaterial) {
    if(nativeOuterActive && nativeOuterActive==view && nativeOuterActive->eye_index>=0 && nativeOuterActive->eye_index<=1){
        const auto eye=unsigned(nativeOuterActive->eye_index);
        if(nativeBindingProbes[eye]<12){
            auto* context=*reinterpret_cast<ID3D11DeviceContext**>(reinterpret_cast<std::uintptr_t>(renderer)+0x76f68);
            if(readable(context,sizeof(void*))){
                ID3D11RenderTargetView* rtv=nullptr;ID3D11DepthStencilView* dsv=nullptr;
                context->OMGetRenderTargets(1,&rtv,&dsv);
                auto desc=[](ID3D11View* resourceView){
                    D3D11_TEXTURE2D_DESC result{};
                    if(resourceView){ID3D11Resource* resource=nullptr;resourceView->GetResource(&resource);
                        ID3D11Texture2D* texture=nullptr;
                        if(resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture)))){texture->GetDesc(&result);texture->Release();}
                        if(resource)resource->Release();}
                    return result;
                };
                const auto color=desc(rtv),depth=desc(dsv);
                UINT n=1;D3D11_VIEWPORT viewport{};context->RSGetViewports(&n,&viewport);
                char line[320]{};std::snprintf(line,sizeof(line),"native_binding eye=%u group=%d color=%ux%u depth=%ux%u viewport=%.0fx%.0f origin=%.0f,%.0f",
                    eye,group,color.Width,color.Height,depth.Width,depth.Height,viewport.Width,viewport.Height,viewport.TopLeftX,viewport.TopLeftY);log_line(line);
                if(rtv)rtv->Release();
                if(dsv)dsv->Release();
                ++nativeBindingProbes[eye];
            }
        }
    }
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    const auto callerRva=caller>=base?caller-base:0;
    const bool eligible=worldVrsOptIn && !worldVrsFailed && worldVrs.ready() &&
        worldVrsRate==benchmarkRequestedRate() && outerVrsContext==worldVrsConfiguredContext &&
        vrs_scope::outer_world_sort_eligible(vrsInput(view,outerSortDepth!=0),outerMainPassMarker,callerRva);
    // Unexpected recursion is rendered at 1x1. Do not re-enable a suspended parent
    // halfway through a draw list or count it as a completed applied scope.
    suspendUnexpectedVrs();
    bool began=false;
    unsigned eye=0;
    if(eligible && selectedOuterView && selectedOuterView->eye_index>=0 && selectedOuterView->eye_index<=1) {
        eye=unsigned(selectedOuterView->eye_index);
        ++worldVrsCounters.attempted[eye];
        began=worldVrs.begin_outer(outerVrsContext);
        if(!began)failWorldVrs("begin_failure; feature disabled; scope not applied");
    }
    const auto generation=vrsInterruptionGeneration;
    ++outerSortDepth;
    worldSortOriginal(renderer,scene,view,group,flags,lastMainPass,optionalMaterial);
    --outerSortDepth;
    if(began && generation==vrsInterruptionGeneration) {
        if(worldVrs.end_outer(outerVrsContext))++worldVrsCounters.applied[eye];
        else failWorldVrs("end_failure; feature disabled; scope not applied; restoration not confirmed");
    }
}
void nativeViewHook(void* pointer,void* objects,void* lights,bool main,bool later){
    // A nested view must see the unmodified native bank. Resume the outer bank
    // afterwards and rebuild its actual viewport through the native cache API.
    if(nativeOuterBindings){
        auto* bindings=nativeOuterBindings;auto* selected=nativeOuterActive;
        const auto* projection=nativeOuterProjection;
        bindings->restore();nativeOuterBindings=nullptr;nativeOuterActive=nullptr;nativeOuterProjection=nullptr;
        nativeViewOriginal(pointer,objects,lights,main,later);
        if(!bindings->apply()){
            nativeOuterFailed=true;nativeQuadSubmissionBlocked=true;
            log_line("native_outer failed to restore nested bindings; quad rendering refused");return;
        }
        nativeOuterBindings=bindings;nativeOuterActive=selected;nativeOuterProjection=projection;
        reinterpret_cast<void(*)(void*,void*)>(base+0x5c6c50)(*address<void*>(0x1b12be8),selected);
        return;
    }
    auto* view=static_cast<ViewSnapshot*>(pointer);
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(!nativeOuterOptIn||nativeOuterFailed||insideHook||view!=nativeOuterExpected||
       caller!=base+0x5de951||!readable(view,sizeof(*view))||view->eye_index<0||view->eye_index>1){
        nativeViewOriginal(pointer,objects,lights,main,later);return;
    }
    if(!benchmarkPath.empty() && benchmarkRequestedRate()!=2){
        nativeViewOriginal(pointer,objects,lights,main,later);
        ++nativeOuterFull[view->eye_index];return;
    }
    // VR wrapper associations initially reference desktop depth targets. Native
    // main-view setup replaces those during the first ordinary scene renders.
    if(view!=nativeOuterWarmupView){nativeOuterWarmupView=view;nativeOuterWarmupCalls=0;}
    if(nativeOuterWarmupCalls<120){
        ++nativeOuterWarmupCalls;
        nativeViewOriginal(pointer,objects,lights,main,later);
        ++nativeOuterFull[view->eye_index];return;
    }
    const auto renderer=*address<std::uintptr_t>(0x1b12be8);
    const auto fullW=*address<unsigned>(0x1b1c654),fullH=*address<unsigned>(0x1b1c658);
    unsigned width=0,height=0;
    if(!nativeOuterDimensions(unsigned(view->eye_index),fullW,fullH,width,height)){
        nativeOuterFailed=true;nativeQuadSubmissionBlocked=true;
        log_line("native_outer incompatible actual full/focus dimensions; runtime sizing requires exact recommendations, no input scaling; quad rendering refused");
        nativeViewOriginal(pointer,objects,lights,main,later);return;
    }
    if(width==fullW&&height==fullH){
        nativeViewOriginal(pointer,objects,lights,main,later);
        if(++nativeOuterCompleted[view->eye_index]==1)log_line("native_outer runtime outer equals full focus size; exact full-size rendering, no pixel reduction");
        return;
    }
    auto& nativeOuterBank=nativeOuterBanks[nativeOuterSeparateBanks?unsigned(view->eye_index):0];
    bool valid=
        readable(reinterpret_cast<void*>(renderer+0x82cd0),8)&&
        !*reinterpret_cast<bool*>(renderer+0x76f50)&&
        *address<float>(0x199cc18)==1.f&&(view->flags&0xc0000000u)==0;
    const auto passes=view->main_passes;
    valid=valid&&passes.end>=passes.begin&&(passes.end-passes.begin)%0x94==0&&
        passes.end-passes.begin==0x94&&readable(reinterpret_cast<void*>(passes.begin),passes.end-passes.begin);
    if(valid)for(auto p=passes.begin;p<passes.end;p+=0x94)valid=valid&&!*reinterpret_cast<bool*>(p);
    const float clearDepth=*reinterpret_cast<unsigned*>(reinterpret_cast<std::byte*>(view)+0x3e8)==1?0.f:1.f;
    if(!valid||!nativeOuterBank.prepare(base,renderer,width,height,clearDepth,readable,log_line)){
        nativeOuterFailed=true;log_line("native_outer refused unsupported view/resource bank; live quad fallback: full-resolution outer render resampled to runtime output; outer render savings unavailable");
        nativeViewOriginal(pointer,objects,lights,main,later);return;
    }
    NativeOuterBindings bindings;
    void* target=nullptr;void* second=nullptr;
    valid=nativeOuterBank.mapTarget(reinterpret_cast<void*>(view->render_target),target)&&
        nativeOuterBank.mapTarget(reinterpret_cast<void*>(view->second_target_unknown),second)&&nativeOuterBank.bind(bindings);
    auto* raw=reinterpret_cast<std::byte*>(view);
    valid=valid&&bindings.add(address<unsigned>(0x1b1c654),width)&&bindings.add(address<unsigned>(0x1b1c658),height)&&
        bindings.add(address<float>(0x1b1c240),float(width))&&bindings.add(address<float>(0x1b1c244),float(height))&&
        bindings.add(reinterpret_cast<unsigned*>(raw+0xdc),width)&&bindings.add(reinterpret_cast<unsigned*>(raw+0xe0),height)&&
        bindings.add(reinterpret_cast<unsigned*>(raw+0xe4),width/2)&&bindings.add(reinterpret_cast<unsigned*>(raw+0xe8),height/2)&&
        bindings.add(reinterpret_cast<unsigned*>(raw+0xec),0u)&&bindings.add(reinterpret_cast<unsigned*>(raw+0xf0),0u)&&
        bindings.add(reinterpret_cast<float*>(raw+0xfc),float(width)/float(height))&&
        bindings.add(&view->render_target,reinterpret_cast<std::uint64_t>(target))&&
        bindings.add(&view->second_target_unknown,reinterpret_cast<std::uint64_t>(second));
    if(!valid||!bindings.apply()){
        nativeOuterFailed=true;log_line("native_outer refused incomplete binding transaction; live quad fallback: full-resolution outer render resampled to runtime output; outer render savings unavailable");
        nativeViewOriginal(pointer,objects,lights,main,later);return;
    }
    const auto savedProjection=view->projection,savedFar=view->far_projection;
    const auto savedFlags=view->flags;
    nativeOuterActive=view;nativeOuterProjection=&savedProjection;nativeOuterBindings=&bindings;
    nativeViewOriginal(pointer,objects,lights,main,later);
    nativeOuterBindings=nullptr;nativeOuterActive=nullptr;nativeOuterProjection=nullptr;
    nativeOuterSource=nativeOuterBank.final(unsigned(view->eye_index));
    bindings.restore();
    view->projection=savedProjection;view->far_projection=savedFar;view->flags=savedFlags;
    derivedOriginal(view,6,false);
    view->flags=savedFlags;
    reinterpret_cast<void(*)(void*,void*)>(base+0x5c6c50)(reinterpret_cast<void*>(renderer),view);
    const auto count=++nativeOuterCompleted[view->eye_index];
    if(count<=4||count%600==0){
        char line[240]{};std::snprintf(line,sizeof(line),"native_outer rendered eye=%d count=%llu actual=%ux%u focus=%ux%u restored=1; PPFX pool remains full-size",
            view->eye_index,static_cast<unsigned long long>(count),width,height,fullW,fullH);log_line(line);
    }
}
void renderHook(void* unknown, bool laterPass) {
    suspendUnexpectedVrs();
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    ID3D11DeviceContext* timingContext=nullptr;
    bool outerTimed=false,outerCounted=false;
    const bool benchmark=!benchmarkPath.empty();
    if(!insideHook && installed.load() && caller==base+0xcd1763 && (benchmark||timingReady())) {
        auto* before=*address<ViewSnapshot*>(0x1a51370);
        const auto renderer=*address<std::uintptr_t>(0x1b12be8);
        if(readable(before,sizeof(*before)) && before->eye_index>=0 && before->eye_index<=1 &&
           readable(reinterpret_cast<void*>(renderer+0x76f68),sizeof(void*))) {
            timingContext=*reinterpret_cast<ID3D11DeviceContext**>(renderer+0x76f68);
            if(readable(timingContext,sizeof(void*))) {
                if(benchmark) {
                    if(before->eye_index==0)benchmarkFrame(timingContext,before);
                    benchmarkPoll(timingContext);
                } else gpuTiming.poll(timingContext);
                if(!benchmark||benchmarkControl.state()==BenchmarkControl::State::measuring) {
                    outerTimed=gpuTiming.start(timingContext,unsigned(before->eye_index));
                    outerCounted=benchmark&&pipelineStats.start(timingContext,unsigned(before->eye_index));
                }
            } else timingContext=nullptr;
        }
    }
    auto* savedOuterView=selectedOuterView;
    auto* savedOuterContext=outerVrsContext;
    const bool savedMainMarker=outerMainPassMarker;
    selectedOuterView=nullptr;outerVrsContext=nullptr;outerMainPassMarker=false;
    if(worldVrsOptIn && !worldVrsFailed && !insideHook && installed.load() &&
       xr_quad_active() && caller==base+0xcd1763) {
        auto* before=*address<ViewSnapshot*>(0x1a51370);
        const auto renderer=*address<std::uintptr_t>(0x1b12be8);
        if(readable(before,sizeof(*before)) && before->eye_index>=0 && before->eye_index<=1 &&
           (before->flags&0xc0000000u)==0 &&
           readable(reinterpret_cast<void*>(renderer+0x76f68),sizeof(void*))) {
            auto* context=*reinterpret_cast<ID3D11DeviceContext**>(renderer+0x76f68);
            if(readable(context,sizeof(void*))) {
                if(!benchmark && before->eye_index==0)prepareWorldVrs(context,before);
                if(worldVrs.ready() && !worldVrsFailed && context==worldVrsConfiguredContext) {
                    selectedOuterView=before;outerVrsContext=context;
                }
            }
        }
    }
    auto* savedNativeExpected=nativeOuterExpected;
    auto* savedNativeSource=nativeOuterSource;
    nativeOuterExpected=nullptr;nativeOuterSource=nullptr;
    if(nativeOuterOptIn&&!nativeOuterFailed&&!insideHook&&installed.load()&&xr_quad_active()&&caller==base+0xcd1763)
        nativeOuterExpected=*address<ViewSnapshot*>(0x1a51370);
    renderOriginal(unknown,laterPass);
    auto* smallOuterSource=nativeOuterSource;
    nativeOuterExpected=savedNativeExpected;nativeOuterSource=savedNativeSource;
    suspendUnexpectedVrs();
    selectedOuterView=savedOuterView;outerVrsContext=savedOuterContext;outerMainPassMarker=savedMainMarker;
    if(outerCounted) pipelineStats.stop(timingContext);
    if(outerTimed) gpuTiming.stop(timingContext);
    if(insideHook || !installed.load() || !xr_quad_active() || nativeQuadSubmissionBlocked) return;
    // Only the verified DYN eye-loop call site. Menus and other render paths are untouched.
    if(caller != base+0xcd1763) return;
    XrView views[4]{};
    if(!xr_frame_views(views)) return;
    auto* view=*address<ViewSnapshot*>(0x1a51370);
    if(!readable(view,sizeof(*view)) || view->eye_index<0 || view->eye_index>1) return;
    const unsigned eye=unsigned(view->eye_index);
    if(view->flags & 0xc0000000u) {
        log_line("engine: focus refused for alternative/oblique view projection");return;
    }
    auto scene=*address<void*>(0x3b4eb88);
    if(!readable(scene,sizeof(void*))) return;
    if(!equalPose(views[eye].pose,views[eye+2].pose)) { log_line("engine: outer/focus pose mismatch");return; }
    auto* source=smallOuterSource?smallOuterSource:*address<ID3D11Texture2D*>(0x1b1c6b8+eye*8);
    if(!readable(source,sizeof(void*))) return;
    auto calls=++renderCalls;
    if(calls<=8 || calls%600==0) {
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        char msg[400];std::snprintf(msg,sizeof(msg),"engine outer eye=%u call=%llu view=%p texture=%p size=%ux%u samples=%u flags=%x",
            eye,static_cast<unsigned long long>(calls),static_cast<void*>(view),static_cast<void*>(source),
            desc.Width,desc.Height,desc.SampleDesc.Count,view->flags);log_line(msg);
    }
    float nearZ{},farZ{};
    std::memcpy(&nearZ,reinterpret_cast<std::byte*>(view)+0x24,4);
    std::memcpy(&farZ,reinterpret_cast<std::byte*>(view)+0x28,4);
    Matrix inner{},innerFar{};
    if(!projection(views[eye+2],nearZ,farZ,inner) || !projection(views[eye+2],nearZ,1000000,innerFar)) {
        log_line("engine: invalid focus projection parameters");return;
    }
    if(view->projection[11]!=1.f || view->projection[15]!=0.f) {
        log_line("engine: unsupported native perspective convention");return;
    }
    inner=replace_fov(view->projection,inner);
    innerFar=replace_fov(view->far_projection,innerFar);
    if(!xr_copy_outer(eye,source)) return;
    // Preserve the exact state the original DYN loop will store as outer camera history.
    const auto outerProjection=view->projection, outerFar=view->far_projection;
    const auto outerCurrent=view->current_transform, outerPrevious=view->previous_transform;
    const auto outerValid=view->previous_transform_valid;
    const auto outerFlags=view->flags;
    auto& history=innerHistory[eye];
    prepareHistory(history,view,outerValid!=0);
    insideHook=true;
    focusView=view;focusProjection=&inner;projectionOverrides=0;skippedMasks=0;
    auto setProjection=reinterpret_cast<SetProjectionFn>(base+0x557810);
    auto derived=reinterpret_cast<DerivedFn>(base+0x557870);
    setProjection(view,inner.data());
    view->far_projection=innerFar;
    view->previous_transform=history.valid ? history.transform : view->current_transform;
    view->previous_transform_valid=history.valid ? 1 : 0;
    // Reuse outer visible objects, but rebuild derived view/frustum and issue fresh draws.
    derived(view,6,false);
    const bool measureFocus=timingContext&&(!benchmark||benchmarkControl.state()==BenchmarkControl::State::measuring);
    const bool focusTimed=measureFocus && gpuTiming.start(timingContext,eye+2);
    const bool focusCounted=measureFocus&&benchmark&&pipelineStats.start(timingContext,eye+2);
    reinterpret_cast<SceneFn>(base+0x5de200)(scene,false,true);
    if(focusCounted) pipelineStats.stop(timingContext);
    if(focusTimed) gpuTiming.stop(timingContext);
    xr_scene_focus_complete(eye);
    history.transform=view->current_transform;history.valid=true;
    focusView=nullptr;focusProjection=nullptr;
    view->current_transform=outerCurrent;
    view->previous_transform=outerPrevious;
    view->previous_transform_valid=outerValid;
    setProjection(view,outerProjection.data());
    view->far_projection=outerFar;
    derived(view,6,false);
    view->flags=outerFlags;
    insideHook=false;
    auto rendered=++focusCalls;
    if(rendered<=8 || rendered%600==0) {
        char msg[256];std::snprintf(msg,sizeof(msg),"engine focus eye=%u count=%llu proj00=%.5f outer00=%.5f overrides=%u masksSkipped=%u",
            eye,static_cast<unsigned long long>(rendered),inner[0],outerProjection[0],projectionOverrides,skippedMasks);log_line(msg);
    }
}
void uiHook(void* object,ID3D11DeviceContext* context,const UiArgs* originalArgs) {
    XrView views[4]{};
    if(!xr_quad_active() || !xr_frame_views(views) || !readable(originalArgs,sizeof(UiArgs)) ||
       !readable(object,0xe8) || originalArgs->mode<1 || originalArgs->mode>3) {
        uiOriginal(object,context,originalArgs);return;
    }
    const bool menu=originalArgs->mode==3 && !xr_outer_rtv(0) && !xr_outer_rtv(1);
    auto targetSlots=reinterpret_cast<ID3D11RenderTargetView**>(static_cast<std::byte*>(object)+0xd8);
    if(menu) {
        // The menu's artwork is sampled from obj+c8, separate from these final eye targets.
        // Mode 3 also runs while a scene is allocated during loading. Only prepare backgrounds
        // if this frame contains no scene captures, then draw UI geometry at all four FOVs.
        ID3D11Texture2D* textures[2]{};
        for(unsigned eye=0;eye<2;++eye) {
            textures[eye]=*address<ID3D11Texture2D*>(0x1b1c6b8+eye*8);
            if(!readable(textures[eye],sizeof(void*)) || !readable(targetSlots[eye],sizeof(void*))) {
                uiOriginal(object,context,originalArgs);return;
            }
            ID3D11Resource* resource{};targetSlots[eye]->GetResource(&resource);
            const bool matches=resource==static_cast<ID3D11Resource*>(textures[eye]);
            if(resource)resource->Release();
            if(!matches) {log_line("engine menu: unexpected final RTV resource");uiOriginal(object,context,originalArgs);return;}
        }
        const float black[4]{};
        for(unsigned eye=0;eye<2;++eye) {
            context->ClearRenderTargetView(targetSlots[eye],black);
            if(!xr_copy_outer(eye,textures[eye])) {
                log_line("engine menu: outer background preparation failed");
                uiOriginal(object,context,originalArgs);return;
            }
        }
    }
    UiArgs focus=*originalArgs;
    for(unsigned eye=0;eye<2;++eye) {
        Matrix outer{},inner{};
        if(!projection(views[eye],0.1f,1000,outer) || !projection(views[eye+2],0.1f,1000,inner)) {
            uiOriginal(object,context,originalArgs);return;
        }
        focus.transform[eye].matrix=remap_fov(originalArgs->transform[eye].matrix,outer,inner);
    }
    uiOriginal(object,context,&focus);
    // Original establishes common UI pipeline state. Eye helper rebinds the draw-specific state.
    UiD3dState restoreState(context);
    auto drawEye=reinterpret_cast<UiEyeFn>(base+0x617fb0);
    for(unsigned eye=0;eye<2;++eye) if(auto* target=xr_outer_rtv(eye)) {
        ID3D11Resource* resource{};target->GetResource(&resource);
        ID3D11Texture2D* texture{};
        const HRESULT query=resource?resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture)):E_POINTER;
        if(resource)resource->Release();
        if(FAILED(query)||!texture) {log_line("engine UI: outer RTV is not a Texture2D");continue;}
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);texture->Release();
        UiTargetScope targetScope(targetSlots[eye],reinterpret_cast<UINT*>(static_cast<std::byte*>(object)+0xbc),target,desc.Width,desc.Height);
        drawEye(object,context,originalArgs,eye);
    }
    auto calls=++uiCalls;
    if(calls<=8 || calls%600==0) {
        char msg[200];std::snprintf(msg,sizeof(msg),"engine UI count=%llu mode=%u menu=%d outerRTV=%d/%d",
            static_cast<unsigned long long>(calls),originalArgs->mode,menu,xr_outer_rtv(0)!=nullptr,xr_outer_rtv(1)!=nullptr);
        log_line(msg);
    }
}
} // namespace

namespace {
struct Hook {std::uintptr_t rva;void* detour;void** original;};
const Hook hooks[]{
    {0xd58a70,reinterpret_cast<void*>(&renderHook),reinterpret_cast<void**>(&renderOriginal)},
    {0x617090,reinterpret_cast<void*>(&uiHook),reinterpret_cast<void**>(&uiOriginal)},
    {0x557810,reinterpret_cast<void*>(&projectionHook),reinterpret_cast<void**>(&projectionOriginal)},
    {0x557870,reinterpret_cast<void*>(&derivedHook),reinterpret_cast<void**>(&derivedOriginal)},
    {0x6188d0,reinterpret_cast<void*>(&maskHook),reinterpret_cast<void**>(&maskOriginal)},
    {0x51d8a0,reinterpret_cast<void*>(&mainPassHook),reinterpret_cast<void**>(&mainPassOriginal)},
    {0x51f040,reinterpret_cast<void*>(&worldSortHook),reinterpret_cast<void**>(&worldSortOriginal)},
    {0x5df6f0,reinterpret_cast<void*>(&nativeViewHook),reinterpret_cast<void**>(&nativeViewOriginal)}
};
void removeHooks(std::size_t count) {
    for(std::size_t i=0;i<count;++i) MH_DisableHook(address<void>(hooks[i].rva));
    for(std::size_t i=0;i<count;++i) MH_RemoveHook(address<void>(hooks[i].rva));
}
}

bool engine_start() {
    if(installed.load()) return true;
    char mode[32]{};GetEnvironmentVariableA("LMU_QV_MODE",mode,sizeof(mode));
    const bool stereoTiming=!std::strcmp(mode,"stereo-timing");
    if(std::strcmp(mode,"quad") && !stereoTiming) {log_line("engine: trace mode; no native hooks");return true;}
    base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!correctDiskBuild() || !matches(0xd58a70,render_anchor) ||
       !matches(0x557810,projection_setter_anchor) || !matches(0x557870,derived_anchor) ||
       !matches(0x617090,ui_anchor) || !matches(0x617fb0,ui_eye_anchor) || !matches(0x5de200,scene_anchor) ||
       !matches(0x6188d0,mask_anchor)) {
        log_line("engine: build or code anchor mismatch; hooks refused");return false;
    }
    char vrsOptIn[8]{};
    const DWORD vrsLength=GetEnvironmentVariableA("LMU_QV_VRS_EXCLUSIVE",vrsOptIn,sizeof(vrsOptIn));
    worldVrsOptIn=!stereoTiming && vrsLength==1 && vrsOptIn[0]=='1';
    char nativeOptIn[8]{};
    const auto nativeLength=GetEnvironmentVariableA("LMU_QV_NATIVE_OUTER",nativeOptIn,sizeof(nativeOptIn));
    nativeOuterOptIn=!stereoTiming&&nativeLength==1&&nativeOptIn[0]=='1';nativeOuterFailed=false;
    nativeQuadSubmissionBlocked=false;
    char runtimeSetting[8]{};GetEnvironmentVariableA("LMU_QV_USE_RUNTIME_SETTINGS",runtimeSetting,sizeof(runtimeSetting));
    nativeRuntimeSettings=!stereoTiming&&!std::strcmp(runtimeSetting,"1");nativeOuterSeparateBanks=false;
    nativeOuterWarmupView=nullptr;nativeOuterWarmupCalls=0;nativeOuterCompleted[0]=nativeOuterCompleted[1]=0;
    nativeOuterFull[0]=nativeOuterFull[1]=0;nativeBindingProbes[0]=nativeBindingProbes[1]=0;
    if(nativeOuterOptIn){
        if(worldVrsOptIn){log_line("engine: initial native outer experiment requires VRS disabled");return false;}
        if(nativeRuntimeSettings){
            uint32_t widths[2]{},heights[2]{},focusW=0,focusH=0;
            for(unsigned eye=0;eye<2;++eye){
                if(!xr_runtime_sizes(eye,widths[eye],heights[eye],focusW,focusH)||!widths[eye]||!heights[eye]||widths[eye]>focusW||heights[eye]>focusH){
                    log_line("engine: runtime outer/focus dimensions unavailable or incompatible; hooks refused");return false;
                }
                char message[192]{};std::snprintf(message,sizeof(message),"engine: runtime exact target eye=%u outer=%ux%u fullFocus=%ux%u",eye,widths[eye],heights[eye],focusW,focusH);log_line(message);
            }
            nativeOuterSeparateBanks=widths[0]!=widths[1]||heights[0]!=heights[1];
        }else{
        char outer[32]{},focus[32]{};
        GetEnvironmentVariableA("LMU_QV_OUTER_PIXELS_PERCENT",outer,sizeof(outer));
        GetEnvironmentVariableA("LMU_QV_FOCUS_PIXELS_PERCENT",focus,sizeof(focus));
        nativeOuterSizes=parse_output_sizes(outer,focus);
        if(!nativeOuterSizes.valid||nativeOuterSizes.outerPercent>=nativeOuterSizes.focusPercent)return false;
        }
        for(const auto& anchor:native_outer_anchors)if(!matches(anchor.rva,anchor.bytes)){
            log_line("engine: native outer ABI anchor mismatch; hooks refused");return false;
        }
        log_line("engine: native outer physical target bank opted in; PPFX intermediate pool remains full-size; experimental");
    }
    char defaultRate[8]{};
    const DWORD defaultRateLength=GetEnvironmentVariableA("LMU_QV_OUTER_SHADING_RATE",defaultRate,sizeof(defaultRate));
    if(!parseVrsDefaultRate(worldVrsOptIn,defaultRate,defaultRateLength,worldVrsDefaultRate)) {
        log_line("engine: LMU_QV_OUTER_SHADING_RATE must be exactly 1, 2 or 4; hooks refused");return false;
    }
    if((worldVrsOptIn||nativeOuterOptIn) && (!matches(0x51d8a0,vrs_scope::kMainPassAnchor) ||
       !matches(0x51f040,vrs_scope::kWorldSortAnchor))) {
        log_line("engine: VRS code anchor mismatch; hooks refused");return false;
    }
    resetWorldVrs();worldVrsCounters={};
    worldVrsFailed=worldVrs.faulted();
    if(worldVrsOptIn) {
        char message[320]{};std::snprintf(message,sizeof(message),
            "engine world VRS: explicit exclusive known-disabled opt-in; default_rate=%u; verified build; caller asserts offline setup and sole VRS ownership; only verified scene sort scopes; other coverage unqualified",
            worldVrsDefaultRate);log_line(message);
    }
    timingTrigger.clear();timingArmed=false;gpuTiming.reset();timingMask=stereoTiming?0x3:0xf;
    pipelineStats.reset();benchmarkPath.clear();benchmarkControl=BenchmarkControl{};benchmarkSnapshot={};
    wchar_t controlPath[32768]{};
    const DWORD controlLength=GetEnvironmentVariableW(L"LMU_QV_BENCH_CONTROL",controlPath,32768);
    if(controlLength&&controlLength<32768&&std::filesystem::path(controlPath).is_absolute()&&!stereoTiming) {
        benchmarkPath=controlPath;
        log_line("engine benchmark: waiting for phaseId rate warmupFrames commands; rates are requests until VRS confirms application");
    }
    wchar_t trigger[32768]{};
    const DWORD length=GetEnvironmentVariableW(L"LMU_QV_GPU_TIMING_TRIGGER",trigger,32768);
    if(benchmarkPath.empty()&&length && length<32768 && std::filesystem::path(trigger).is_absolute()) {
        timingTrigger=trigger;
        log_line("engine GPU timing: waiting for explicit trigger file");
    }
    const std::size_t hookCount=stereoTiming?1:(nativeOuterOptIn?std::size(hooks):(worldVrsOptIn?7:5));
    auto status=MH_Initialize();
    if(status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED) {log_line("engine: MH_Initialize failed");return false;}
    std::size_t created=0;
    for(std::size_t i=0;i<hookCount;++i) {
        const auto& hook=hooks[i];
        status=MH_CreateHook(address<void>(hook.rva),hook.detour,hook.original);
        if(status!=MH_OK) {removeHooks(created);log_line("engine: hook creation failed");return false;}
        ++created;
    }
    for(std::size_t i=0;i<hookCount;++i) {
        const auto& hook=hooks[i];
        if(MH_EnableHook(address<void>(hook.rva))!=MH_OK) {
            removeHooks(created);log_line("engine: hook enable failed");return false;
        }
    }
    innerHistory[0]={};innerHistory[1]={};
    installedHookCount=hookCount;
    installed.store(true);
    log_line(stereoTiming ? "engine: verified build; stereo timing render hook only; no quad repeats or projection/UI changes" :
                           "engine: verified build; render/UI/projection/mask hooks enabled");
    return true;
}
void engine_stop() {
    nativeOuterOptIn=false;
    for(auto& bank:nativeOuterBanks)if(!bank.reset())log_line("native_outer teardown on different thread; native bank retained until process exit");
    if(!installed.exchange(false)) {
        resetWorldVrs();worldVrsOptIn=false;worldVrsDefaultRate=1;worldVrsCounters={};
        gpuTiming.reset();pipelineStats.reset();benchmarkPath.clear();benchmarkControl=BenchmarkControl{};
        benchmarkSnapshot={};timingTrigger.clear();timingArmed=false;return;
    }
    removeHooks(installedHookCount);installedHookCount=0;
    logVrsCounters();resetWorldVrs();worldVrsOptIn=false;worldVrsDefaultRate=1;worldVrsCounters={};
    gpuTiming.reset();pipelineStats.reset();benchmarkPath.clear();benchmarkControl=BenchmarkControl{};
    benchmarkSnapshot={};timingTrigger.clear();timingArmed=false;
    log_line("engine: hooks removed");
}
} // namespace lmuqv
