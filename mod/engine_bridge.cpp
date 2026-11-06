#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "bridge.hpp"
#include "caller_address.hpp"
#include "quad_math.hpp"
#include "ui_d3d_state.hpp"
#include "ui_plane_scale.hpp"
#include "gpu_timing.hpp"
#include "render_profile.hpp"
#include "stereo_audit.hpp"
#include "sps_cpu_profile_hooks.hpp"
#include "native_stereo_focus_inputs.hpp"
#include "native_stereo_owner_inputs.hpp"
#include "native_stereo_pose_adapter.hpp"
#include "native_stereo_view_producer.hpp"
#include "native_sps.hpp"
#include "native_sps_runtime.hpp"
#include "pipeline_stats.hpp"
#include "benchmark_control.hpp"
#include "code_anchors.hpp"
#include "nv_vrs.hpp"
#include "vrs_scope_policy.hpp"
#include "native_outer_bank.hpp"
#include "native_outer_anchors.hpp"
#include "native_upload_reuse.hpp"
#include "native_upload_batch.hpp"
#include "xr_sizes.hpp"
#include "view_crop.hpp"
#include "native_xr_views.hpp"
#include "resource_diagnostics.hpp"
#include "../prototype/lmu_reconstructed.hpp"
#include "vendor/minhook-1.3.4/include/MinHook.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
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
using FovScaleFn = const float* (*)(void*);
using NativePrepareFn=int(*)(void*,void*,void*,std::uint32_t,bool);
NativePrepareFn nativePrepareOriginal{};bool nativePrepareHooked=false;
constexpr std::uintptr_t nativePrepareRva=0x5629e0;
constexpr std::array<std::uint8_t,20> nativePrepareAnchor{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x30};
// Verified caller at 0x562aca passes RCX/RDX/R8, R9b and one stack byte,
// then continues preparation without consuming a return value.
using NativeTransformFn=void(*)(void*,void*,void*,bool,bool);
NativeTransformFn nativeTransformOriginal{};bool nativeTransformHooked=false;
constexpr std::uintptr_t nativeTransformRva=0x565260;
constexpr std::array<std::uint8_t,26> nativeTransformAnchor{
    0x48,0x8b,0xc4,0x48,0x89,0x48,0x08,0x55,0x53,0x56,0x57,0x41,0x56,
    0x41,0x57,0x48,0x8d,0x68,0xb8,0x48,0x81,0xec,0x18,0x01,0x00,0x00};
using NativeClassifyFn = uint32_t(__fastcall *)(void *, void *, const float *, bool, float);
NativeClassifyFn nativeClassifyOriginal{};
bool nativeClassifyHooked = false;
constexpr uintptr_t nativeClassifyRva = 0x5306a0;
constexpr std::array<uint8_t, 24> nativeClassifyAnchor{
    0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48,
    0x89, 0x70, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x60};
using NativeUploadFn=void(*)(void*,void*,ID3D11DeviceContext*,void*);
NativeUploadFn nativeUploadOriginal{};std::atomic<bool> nativeUploadHooked{false},nativeUploadOptIn{false},nativeUploadOuterOptIn{false};
constexpr std::uintptr_t nativeUploadRva=0x5cac50;
constexpr std::array<std::uint8_t,20> nativeUploadAnchor{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x40};
NativeUploadReuse nativeUploadReuse;
NativeUploadBatch nativeUploadBatch;std::atomic<bool> nativeUploadBatchOptIn{false};
uint64_t batchFailureLogged=0;unsigned batchFailureMessages=0;
void logUploadBatchStats(const char* stage){
    if(!diagnostics_enabled())return;const auto& c=nativeUploadBatch.counters();const auto& reuse=nativeUploadReuse.counters();char line[1024]{};
    std::snprintf(line,sizeof(line),"cpu_upload_batch %s scopes=%llu outer_calls=%llu focus_calls=%llu unchanged_maps_skipped=%llu unchanged_bytes_skipped=%llu native_maps_replaced=%llu batch_maps=%llu restoration_maps=%llu net_maps_removed=%lld native_bytes_replaced=%llu batch_bytes=%llu large_maps=%llu invalidations=%llu failures=%llu last_failure=%s range=%llu hr=%08lx; native draws unchanged; counters are not a frametime benchmark",stage,
        static_cast<unsigned long long>(c.scopes),static_cast<unsigned long long>(c.outerCalls),static_cast<unsigned long long>(c.focusCalls),static_cast<unsigned long long>(reuse.skipped),static_cast<unsigned long long>(reuse.bytesSkipped),static_cast<unsigned long long>(c.nativeMapsReplaced),static_cast<unsigned long long>(c.batchMaps),static_cast<unsigned long long>(c.restoreMaps),static_cast<long long>(reuse.skipped)+static_cast<long long>(c.nativeMapsReplaced)-static_cast<long long>(c.batchMaps)-static_cast<long long>(c.restoreMaps),static_cast<unsigned long long>(c.nativeBytesReplaced),static_cast<unsigned long long>(c.batchBytes),static_cast<unsigned long long>(c.largeMaps),static_cast<unsigned long long>(c.invalidations),static_cast<unsigned long long>(c.mapFailures),nativeUploadBatch.failure_reason(),static_cast<unsigned long long>(nativeUploadBatch.failure_range()),static_cast<unsigned long>(nativeUploadBatch.failure_hr()));log_line(line);
}
std::recursive_mutex nativeUploadMutex;
thread_local void* selectedUploadCache=nullptr;
thread_local ID3D11DeviceContext* selectedUploadContext=nullptr;

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
FovScaleFn fovScaleOriginal{};
constexpr std::uintptr_t fovScaleRva=0x5bb4c0;
constexpr std::array<std::uint8_t,8> fovScaleAnchor{0x48,0x8d,0x81,0xe0,0x70,0x07,0x00,0xc3};
constexpr std::array<std::uintptr_t,3> fovScaleCallers{0x5bd63c,0x5bd652,0x5baaf1};
constexpr std::array<std::uint8_t,6> fovScaleCallAnchor{0xff,0x90,0x78,0x0c,0x00,0x00};
bool fovScaleLogged=false;
NativeOuterBank nativeOuterBanks[2];
bool nativeOuterOptIn=false,nativeOuterFailed=false,nativeOuterSsrOptIn=false;
// Failure to allocate an optional small bank is recoverable. A size mismatch
// or failed restoration is not: keep those separate from the performance path.
bool nativeQuadSubmissionBlocked=false;
XrOutputSizes nativeOuterSizes;
CropPercent nativeCropWidth,nativeCropHeight;
bool nativeRuntimeSettings=false,nativeOuterSeparateBanks=false;
const float* fovScaleHook(void* renderer){
    const auto* original=fovScaleOriginal(renderer);
    const auto caller=reinterpret_cast<std::uintptr_t>(LMU_CALLER_ADDRESS);
    // Only native eye allocation/crop offsets and raw projection consume unity.
    // Configuration readers keep seeing the user's original crop preference.
    bool renderCaller=false;
    for(auto rva:fovScaleCallers)renderCaller|=caller==base+rva;
    if(!nativeRuntimeSettings||!renderCaller)return original;
    if(!fovScaleLogged){
        char message[224]{};
        std::snprintf(message,sizeof(message),
            "engine: runtime FOV crop bypass nativeScale=%.6g/%.6g effectiveScale=1/1 callerRVA=%llx; saved settings preserved",
            original[0],original[1],static_cast<unsigned long long>(caller-base));
        log_line(message);fovScaleLogged=true;
    }
    static constexpr float runtimeScale[2]{1.f,1.f};
    return runtimeScale;
}
bool nativeOuterDimensions(unsigned eye,unsigned fullW,unsigned fullH,unsigned& width,unsigned& height){
    if(nativeRuntimeSettings){
        uint32_t focusW=0,focusH=0;
        if(!xr_runtime_sizes(eye,width,height,focusW,focusH)||fullW!=focusW||fullH!=focusH)return false;
    }else{width=crop_dimension(outer_dimension(fullW,nativeOuterSizes),nativeCropWidth.percent);height=crop_dimension(outer_dimension(fullH,nativeOuterSizes),nativeCropHeight.percent);}
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
unsigned nativeOuterPrepareFailures=0,nativeOuterRetryCalls=0;
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
bool stereoVrsMode=false;
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
bool spsPreparationOptIn=false,spsPreparedLogged=false,spsPreparationRefusedLogged=false;
bool spsRenderingOptIn=false;
thread_local StereoFrameIdentity spsRenderFrameIdentity;
struct SpsRenderFrameScope {
    StereoFrameIdentity previous;
    explicit SpsRenderFrameScope(const StereoFrameIdentity& key):previous(spsRenderFrameIdentity){spsRenderFrameIdentity=key;}
    ~SpsRenderFrameScope(){spsRenderFrameIdentity=previous;}
};
NativeStereoFocusFrame spsFocusFrame;
using NativeCameraStateFn=void(*)(void*,void*);
using NativeCameraPoseFn=void(*)(void*,float,float,float,float,float,float);
NativeCameraStateFn nativeCameraStateOriginal{};
NativeCameraPoseFn nativeCameraPoseOriginal{};
bool nativeCameraStateHooked=false,nativeCameraPoseHooked=false;
constexpr uintptr_t nativeCameraStateRva=0x5bb600,nativeCameraPoseRva=0x556fa0;
// Complete leaf through RET; this function has no unwind-table entry.
constexpr std::array<uint8_t,114> nativeCameraStateAnchor{
    0x48,0x8d,0x05,0x29,0x08,0x56,0x01,0xb9,0x08,0x00,0x00,0x00,0x0f,0x1f,0x40,0x00,
    0x48,0x8d,0x92,0x80,0x00,0x00,0x00,0x0f,0x10,0x00,0x48,0x8d,0x80,0x80,0x00,0x00,
    0x00,0x0f,0x11,0x42,0x80,0x0f,0x10,0x48,0x90,0x0f,0x11,0x4a,0x90,0x0f,0x10,0x40,
    0xa0,0x0f,0x11,0x42,0xa0,0x0f,0x10,0x48,0xb0,0x0f,0x11,0x4a,0xb0,0x0f,0x10,0x40,
    0xc0,0x0f,0x11,0x42,0xc0,0x0f,0x10,0x48,0xd0,0x0f,0x11,0x4a,0xd0,0x0f,0x10,0x40,
    0xe0,0x0f,0x11,0x42,0xe0,0x0f,0x10,0x48,0xf0,0x0f,0x11,0x4a,0xf0,0x48,0x83,0xe9,
    0x01,0x75,0xad,0x0f,0x10,0x00,0x0f,0x11,0x02,0x48,0x8b,0x48,0x10,0x48,0x89,0x4a,
    0x10,0xc3};
constexpr std::array<uint8_t,17> nativeCameraPoseAnchor{
    0x40,0x53,0x48,0x83,0xec,0x50,0x0f,0x29,0x74,0x24,0x40,0x48,0x8b,0xd9,0x0f,0x29,0x7c};
thread_local NativeStereoWorldPoseFrame spsWorldPoseFrame;
thread_local NativeStereoWorldPosePair spsExpectedWorldPose;
thread_local uintptr_t spsExpectedPoseView=0;
thread_local StereoFrameIdentity spsExpectedPoseIdentity;
thread_local bool spsAwaitingRightPose=false;
std::atomic<uint64_t> spsNativePoseEpoch{1};
thread_local uint64_t spsWorldPoseEpoch=0;
thread_local const char* spsNativePoseSourceReason="native camera-state boundary not observed";
thread_local uintptr_t spsNativeStateCaller=0,spsNativePoseCaller=0;
bool spsNativePoseRejected=false;
bool spsNativeViewSettingsVerified=false,spsNativeViewRejected=false,spsNativeViewsLogged=false,spsNativeViewsRefusalLogged=false;
NativeStereoViewInputFrame spsViewInputFrame;
uint64_t spsNativeViewProduced=0,spsNativeViewRefused=0,spsNativeViewMatches[2]{};
uint64_t spsNativePoseProduced=0,spsNativePoseFallback=0;
uint64_t spsNativeRightPoseChecks=0,spsNativeRightPoseMismatches=0;
std::array<uint64_t,4> spsRightMismatchKinds{}; // mode, position, basis, translation
bool spsNativePoseLogged=false;
using NativeOwnerFn=void(*)(void*,void*,void*,float);
NativeOwnerFn nativeOwnerOriginal{};
std::atomic<bool> nativeOwnerInputEnabled{false};bool nativeOwnerInputHooked=false;
constexpr uintptr_t nativeOwnerRva=0x5e1140;
constexpr std::array<uint8_t,15> nativeOwnerAnchor{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x48,0x83,0xec,0x30};
NativeStereoOwnerInputFrame spsOwnerFrame;
NativeStereoOwnerCaptureOnce spsOwnerCapture;
bool spsOwnerCaptureOptIn=false;
std::mutex spsOwnerMutex;
thread_local StereoFrameIdentity spsOwnerFrameIdentity;
uint64_t spsOwnerFrames=0,spsOwnerRoots=0,spsOwnerRefusals=0;
uint64_t spsOwnerAdmissionReadyRoots=0,spsOwnerWorldRefreshOwners=0,spsOwnerBoundsAdmissionOwners=0;
bool spsOwnerReadyLogged=false,spsOwnerRefusalLogged=false;
struct SpsPreparationCounts {uint64_t prepared=0,refused=0,used[2]{},fallback[2]{};} spsPreparationCounts;
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
        const auto caller=reinterpret_cast<std::uintptr_t>(LMU_CALLER_ADDRESS);
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
// Keep these as real frames: caller-slot capture and Windows unwind recovery
// must observe the native caller rather than an inlined helper's parent.
#if defined(_MSC_VER)
#define LMU_NATIVE_NOINLINE __declspec(noinline)
#else
#define LMU_NATIVE_NOINLINE __attribute__((noinline))
#endif
bool beginNativeCameraPair(const CONTEXT& caller,uintptr_t view,uintptr_t manager){
    if(!view||!manager||caller.R12!=0||caller.R13!=1||caller.R14!=view||caller.R15!=manager)return false;
    // A mismatch invalidates its current pair. A fresh native left update
    // must pass every numeric comparison again before any draw is replaced.
    spsNativePoseRejected=false;spsNativeViewRejected=false;return true;
}
LMU_NATIVE_NOINLINE void nativeCameraStateHook(void* renderer,void* destination){
    const auto caller=reinterpret_cast<uintptr_t>(LMU_CALLER_ADDRESS);
    const auto slot=reinterpret_cast<uintptr_t>(LMU_CALLER_RETURN_SLOT);
    nativeCameraStateOriginal(renderer,destination);
    if(!spsPreparationOptIn)return;
    spsNativeStateCaller=caller;
    if(caller!=base+0xc8c17a)return;
    NativeStereoFpGuard preserve;
    const auto epoch=spsNativePoseEpoch.load();
    if(spsWorldPoseEpoch!=epoch){
        spsExpectedWorldPose={};spsExpectedPoseView=0;spsAwaitingRightPose=false;spsWorldPoseEpoch=epoch;
    }
    spsWorldPoseFrame.reset();
    // The hook runs after XR's native camera update. Do not locate/lock a new
    // XR frame here; the render hook later attaches the rendered identity.
    spsNativePoseSourceReason="native camera globals unavailable";
    if(!readable(address<void>(0x1a51370),8)||!readable(address<void>(0x1a51380),8)||
       !readable(address<void>(0x1a51400),4))return;
    spsNativePoseSourceReason="unsupported native camera mode";
    if(*address<int32_t>(0x1a51400)!=0)return;
    const auto view=*address<uintptr_t>(0x1a51370),manager=*address<uintptr_t>(0x1a51380);
    spsNativePoseSourceReason="native cockpit manager/view unavailable";
    if(!readable(reinterpret_cast<void*>(manager),8)||
       *reinterpret_cast<uintptr_t*>(manager)!=base+0x17c5f28||
       !readable(reinterpret_cast<void*>(view),0x4b8))return;
    uint64_t parent=0;std::memcpy(&parent,reinterpret_cast<const std::byte*>(view)+0x4b0,8);
    spsNativePoseSourceReason="attached native camera";if(parent)return;
    // The ordinary setter adds these six view offsets. Other modes retain the
    // native path until their complete rotation/translation model is available.
    std::array<float,6> offsets{};std::memcpy(offsets.data(),reinterpret_cast<const std::byte*>(view)+0xc4,24);
    spsNativePoseSourceReason="nonzero native camera offsets";for(float value:offsets)if(value!=0)return;
    CONTEXT context;NativeStereoWorldPoseInput input;
    spsNativePoseSourceReason="native camera caller unwind failed";
    if(!capture_native_caller_context(caller,slot,context,readable))return;
    if(context.R12==1&&context.R13==1&&context.R14==view&&context.R15==manager){
        spsAwaitingRightPose=spsExpectedWorldPose.complete&&spsExpectedPoseView==view;return;
    }
    spsAwaitingRightPose=false;spsExpectedWorldPose={};spsExpectedPoseView=0;
    beginNativeCameraPair(context,view,manager);
    spsNativePoseSourceReason="native camera caller/input layout differs";
    if(capture_native_stereo_pose_input(context,destination,view,manager,readable,input)){
        std::copy_n(offsets.begin(),3,input.viewTranslation.begin());
        if(spsNativeViewSettingsVerified&&!spsNativeViewRejected)
            capture_native_stereo_cockpit_settings(context,view,manager,readable,input.settings);
        spsWorldPoseFrame.begin(view,input);spsNativePoseSourceReason=spsWorldPoseFrame.reason();
    }
}
LMU_NATIVE_NOINLINE void nativeCameraPoseHook(void* pointer,float x,float y,float z,float pitch,float yaw,float roll){
    const auto caller=reinterpret_cast<uintptr_t>(LMU_CALLER_ADDRESS);
    nativeCameraPoseOriginal(pointer,x,y,z,pitch,yaw,roll);
    if(!spsPreparationOptIn)return;
    spsNativePoseCaller=caller;
    if(caller!=base+0xc8c493)return;
    NativeStereoFpGuard preserve;
    const auto* view=static_cast<const std::byte*>(pointer);
    if(!readable(view,0x22c)){spsWorldPoseFrame.reset();return;}
    Matrix current{};std::array<float,3> position{};int32_t mode=0;uint32_t flags=0;
    std::memcpy(current.data(),view+0x1dc,64);std::memcpy(position.data(),view+0x220,12);
    std::memcpy(&mode,view+0x21c,4);std::memcpy(&flags,view+0x1d8,4);
    if(spsAwaitingRightPose&&spsExpectedPoseView==reinterpret_cast<uintptr_t>(pointer)){
        ++spsNativeRightPoseChecks;
        const auto& expected=spsExpectedWorldPose.current[1];const auto& expectedPosition=spsExpectedWorldPose.constantPosition[1];
        const std::array<bool,4> different{mode!=1,!native_world_pose_detail::same(position,expectedPosition),
            std::memcmp(current.data(),expected.data(),12*sizeof(float))!=0,
            std::memcmp(current.data()+12,expected.data()+12,4*sizeof(float))!=0};
        if(different[0]||different[1]||different[2]||different[3]){
            spsNativePoseRejected=true;
            ++spsNativeRightPoseMismatches;for(unsigned i=0;i<4;++i)spsRightMismatchKinds[i]+=different[i];
            // Count every failure; keep expensive formatting to eight examples.
            if(spsNativeRightPoseMismatches<=8){
                uint32_t expectedBits[3]{},actualBits[3]{};std::memcpy(expectedBits,expectedPosition.data(),12);std::memcpy(actualBits,position.data(),12);
                unsigned first=0;uint32_t expectedWord=0,actualWord=0;
                for(;first<16;++first){std::memcpy(&expectedWord,&expected[first],4);std::memcpy(&actualWord,&current[first],4);if(expectedWord!=actualWord)break;}
                // Use the stored pair identity. A diagnostic must not call an
                // XR accessor that can locate or lock a new rendered frame.
                char line[896]{};
                std::snprintf(line,sizeof(line),"sps_prepare right_mismatch sample=%llu expected_frame=%llu expected_time=%lld mode=%d position=%d basis=%d translation=%d expected_position=%.9g/%.9g/%.9g actual_position=%.9g/%.9g/%.9g expected_bits=%08x/%08x/%08x actual_bits=%08x/%08x/%08x first_matrix=%u matrix_bits=%08x/%08x; current pair refused",
                    static_cast<unsigned long long>(spsNativeRightPoseMismatches),static_cast<unsigned long long>(spsExpectedPoseIdentity.frame),
                    static_cast<long long>(spsExpectedPoseIdentity.displayTime),
                    int(different[0]),int(different[1]),int(different[2]),int(different[3]),
                    expectedPosition[0],expectedPosition[1],expectedPosition[2],position[0],position[1],position[2],
                    expectedBits[0],expectedBits[1],expectedBits[2],actualBits[0],actualBits[1],actualBits[2],first,expectedWord,actualWord);log_line(line);
            }
        }
        spsAwaitingRightPose=false;spsExpectedWorldPose={};spsExpectedPoseView=0;spsExpectedPoseIdentity={};return;
    }
    spsWorldPoseFrame.finish(reinterpret_cast<uintptr_t>(pointer),current,position,mode,flags);
    spsNativePoseSourceReason=spsWorldPoseFrame.reason();
}
#undef LMU_NATIVE_NOINLINE
void diagnoseNativeEye(const char* stage,unsigned eye,ID3D11Texture2D* texture,ID3D11RenderTargetView* rtv,
                       ID3D11ShaderResourceView* srv,ID3D11DeviceContext* context){
    if(!diagnostics_enabled())return;
    diagnose_eye_resource(stage,eye,readable(texture,sizeof(void*))?texture:nullptr,
        readable(rtv,sizeof(void*))?rtv:nullptr,readable(srv,sizeof(void*))?srv:nullptr,
        readable(context,sizeof(void*))?context:nullptr,log_line);
}
bool correctProcess() {
    wchar_t path[32768]{};
    const auto length=GetModuleFileNameW(nullptr,path,32768);
    return length && length<32768 &&
        !_wcsicmp(std::filesystem::path(path).filename().c_str(),L"Le Mans Ultimate.exe");
}
template<std::size_t N> bool matches(std::uintptr_t rva,const std::array<std::uint8_t,N>& bytes) {
    return readable(address<void>(rva),N) && !std::memcmp(address<void>(rva),bytes.data(),N);
}
Fov fov(const XrView& v) { return {v.fov.angleLeft,v.fov.angleRight,v.fov.angleUp,v.fov.angleDown}; }
bool projectionScales(float& sx,float& sy) {
    if(nativeRuntimeSettings){sx=sy=1.f;return true;}
    auto renderer=*address<std::uintptr_t>(0x1b12be8);
    if(!readable(reinterpret_cast<void*>(renderer+0x770e0),8)) return false;
    std::memcpy(&sx,reinterpret_cast<void*>(renderer+0x770e0),4);
    std::memcpy(&sy,reinterpret_cast<void*>(renderer+0x770e4),4);
    return true;
}
bool projection(const XrView& v,float nearZ,float farZ,Matrix& result) {
    float sx{},sy{};if(!projectionScales(sx,sy))return false;
    return make_engine_projection(fov(v),nearZ,farZ,sx,sy,result);
}
PFN_xrEnumerateViewConfigurationViews nativeEnumerateOriginal{};
constexpr std::array<std::uint8_t,16> nativeEnumerateAnchor{
    0x40,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xc8,0x00,0x00};
XrResult XRAPI_CALL nativeEnumerateHook(XrInstance instance,XrSystemId system,XrViewConfigurationType type,
                                      uint32_t capacity,uint32_t* count,XrViewConfigurationView* views){
    const auto caller=reinterpret_cast<std::uintptr_t>(LMU_CALLER_ADDRESS);
    const auto result=nativeEnumerateOriginal(instance,system,type,capacity,count,views);
    if(!nativeRuntimeSettings||caller!=base+0x5bd214||XR_FAILED(result))return result;
    uint32_t ow=0,oh=0,w=0,h=0;
    if(!count||!readable(count,sizeof(*count))||capacity<2||*count!=2||!readable(views,2*sizeof(*views))||
       !xr_runtime_sizes(0,ow,oh,w,h)){
        nativeQuadSubmissionBlocked=true;
        log_line("native XR enumeration invalid final game-facing result; quad scene blocked, original enumeration preserved");return result;
    }
    char line[320]{};std::snprintf(line,sizeof(line),
        "native XR boundary after upper layers before normalization eye0=%ux%u eye1=%ux%u canonical=%ux%u",
        views[0].recommendedImageRectWidth,views[0].recommendedImageRectHeight,
        views[1].recommendedImageRectWidth,views[1].recommendedImageRectHeight,w,h);log_line(line);
    if(!normalize_native_views(true,caller-base,type,capacity,*count,views,w,h)){
        nativeQuadSubmissionBlocked=true;log_line("native XR normalization failed; quad scene blocked, original enumeration preserved");return result;
    }
    log_line("native XR boundary: runtime focus dimensions delivered directly to LMU; extra recommendation scaling bypassed");
    return result;
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
    if(!worldVrsOptIn || worldVrsFailed || !context || !view || view->eye_index<0 || view->eye_index>1 ||
       (!stereoVrsMode&&view->eye_index!=0) || (!xr_quad_active()&&!xr_vrs_active()))return false;
    auto* texture=*address<ID3D11Texture2D*>(0x1b1c6b8+(stereoVrsMode?view->eye_index*8:0));
    if(!readable(texture,sizeof(void*)))return false;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(!desc.Width || !desc.Height)return false;
    ID3D11Device* device=nullptr;context->GetDevice(&device);
    if(!device) {failWorldVrs("configure_failure: no device");return false;}
    const unsigned rate=benchmarkRequestedRate();
    const bool unchanged=worldVrs.ready() && worldVrsDevice==device &&
        worldVrsConfiguredContext==context && worldVrsWidth==desc.Width &&
        worldVrsHeight==desc.Height && worldVrsRate==rate;
    if(unchanged) {
        device->Release();
        if(stereoVrsMode){VrsFocus focus;if(!xr_vrs_render_focus(unsigned(view->eye_index),view->projection.data(),focus))return false;
            return worldVrs.update_focus(context,focus);}
        return true;
    }
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
    if(stereoVrsMode){VrsFocus focus;if(!xr_vrs_render_focus(unsigned(view->eye_index),view->projection.data(),focus))return false;
        return worldVrs.update_focus(context,focus);}
    return true;
}
void logVrsCounters() {
    if(!diagnostics_enabled())return;
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
    const auto caller=reinterpret_cast<std::uintptr_t>(LMU_CALLER_ADDRESS);
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
    StereoAuditGroup auditGroup(group);
    const auto caller = reinterpret_cast<std::uintptr_t>(LMU_CALLER_ADDRESS);
    const auto callerRva = caller >= base ? caller - base : 0;
    SpsCpuRangeScope cpuRange(view,group,callerRva);
    // Capture the ordered opaque inputs before native last-pass cleanup releases
    // their lists. This observes scalars only; it does not retain or skip a draw.
    const StereoNativePassInfo nativePassInfo{
        scene, view, callerRva <= UINT32_MAX ? uint32_t(callerRva) : 0,
        flags, lastMainPass, readable,
        reinterpret_cast<const void*>(base+0x1B1BE30),
        reinterpret_cast<const void*>(base+0x1A51400),
        reinterpret_cast<const void*>(base+0x1A51370)};
    const auto nativePass = stereo_audit_native_pass_begin(nativePassInfo);
    const auto savedUploadCache=selectedUploadCache;const auto savedUploadContext=selectedUploadContext;
    std::unique_lock<std::recursive_mutex> uploadLock(nativeUploadMutex,std::defer_lock);
    bool uploadScope=false;
    if(selectedUploadCache){nativeUploadBatch.end();nativeUploadReuse.end();} // Nested groups invalidate exclusive scope assumptions.
    const bool batchEligible=nativeUploadBatchOptIn&&view&&(view==focusView||view==nativeOuterExpected);
    // Outer reuse has the same per-group lifetime as focus reuse. It does not
    // retain constants or skip preparation between outer and focus traversals.
    const bool reuseEligible=nativeUploadOptIn&&view&&
        (view==focusView||(nativeUploadOuterOptIn&&view==nativeOuterExpected));
    if((batchEligible||reuseEligible)&&nativeUploadHooked&&group==31&&!selectedUploadCache){
        const auto object=reinterpret_cast<std::uintptr_t>(renderer);
        if(readable(reinterpret_cast<void*>(object+0x77fc8),0x2660)&&readable(reinterpret_cast<void*>(object+0x77940),0x200)&&readable(reinterpret_cast<void*>(object+0x76f68),sizeof(void*))){
            auto* context=*reinterpret_cast<ID3D11DeviceContext**>(object+0x76f68);
            if(readable(context,sizeof(void*))){uploadLock.lock();selectedUploadCache=reinterpret_cast<void*>(object+0x77fc8);selectedUploadContext=context;
                if(batchEligible)nativeUploadBatch.begin(selectedUploadCache,context,diagnostics_enabled(),view==focusView);
                nativeUploadReuse.begin(selectedUploadCache,context,diagnostics_enabled());uploadScope=true;}
        }
    }
    const bool profileSort=renderProfile.collecting()&&view&&(view==nativeOuterExpected||view==focusView);
    const auto sortEye=profileSort?unsigned(static_cast<ViewSnapshot*>(view)->eye_index):0;
    ProfileScope sortProfile(profile_eye(focusView?ProfileSlot::sort_focus0:ProfileSlot::sort_outer0,sortEye),profileSort&&sortEye<2);
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
    bool targetMatches=true;
    if(stereoVrsMode&&outerVrsContext){
        UINT count=1;D3D11_VIEWPORT viewport{};outerVrsContext->RSGetViewports(&count,&viewport);
        targetMatches=count==1&&viewport.TopLeftX==0&&viewport.TopLeftY==0&&
            viewport.Width==float(worldVrsWidth)&&viewport.Height==float(worldVrsHeight);
    }
    const bool eligible=targetMatches&&worldVrsOptIn && !worldVrsFailed && worldVrs.ready() &&
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
    ID3D11DeviceContext* spsContext=nullptr;const NativeStereoViewInputs* spsViews=nullptr;unsigned spsEye=2;
    if(spsRenderingOptIn&&insideHook&&view==focusView&&group==31&&callerRva==0x51df1d&&
       spsRenderFrameIdentity.valid()&&!spsNativeViewRejected&&readable(view,sizeof(ViewSnapshot))){
        const auto selectedEye=static_cast<ViewSnapshot*>(view)->eye_index;
        const auto pointer=reinterpret_cast<uintptr_t>(renderer);
        if(selectedEye>=0&&selectedEye<2&&pointer<=(std::numeric_limits<uintptr_t>::max)()-0x76f68&&
           readable(reinterpret_cast<void*>(pointer+0x76f68),sizeof(spsContext))){
            std::memcpy(&spsContext,reinterpret_cast<void*>(pointer+0x76f68),sizeof(spsContext));
            spsViews=spsViewInputFrame.inputs(spsRenderFrameIdentity);spsEye=unsigned(selectedEye);
        }
    }
    NativeSpsGroupScope spsGroup(spsContext,reinterpret_cast<uintptr_t>(renderer),reinterpret_cast<uintptr_t>(view),spsEye,spsViews,readable);
    worldSortOriginal(renderer,scene,view,group,flags,lastMainPass,optionalMaterial);
    stereo_audit_native_pass_end(nativePass);
    if(uploadScope){nativeUploadBatch.end();nativeUploadReuse.end();}selectedUploadCache=savedUploadCache;selectedUploadContext=savedUploadContext;
    --outerSortDepth;
    if(began && generation==vrsInterruptionGeneration) {
        if(worldVrs.end_outer(outerVrsContext))++worldVrsCounters.applied[eye];
        else failWorldVrs("end_failure; feature disabled; scope not applied; restoration not confirmed");
    }
}
uint32_t __fastcall nativeClassifyHook(void *candidate, void *view, const float *position,
                                       bool skipDistanceBounds, float bias) {
  const auto caller = reinterpret_cast<uintptr_t>(LMU_CALLER_ADDRESS);
  // Entry MXCSR is saved before begin; post-call MXCSR and EAX32 are
  // captured before any end bookkeeping. Original arguments stay untouched.
  return invoke_native_stereo_classify(
      [&](uint32_t entry) {
        const StereoNativeClassifyInfo info{
            {candidate, view, position, skipDistanceBounds, bias, address<void>(0x1a51370)},
            caller == base + 0x41c8b4 ? 0x41c8b4u : 0u,
            readable};
        return stereo_audit_native_classify_begin(info, entry);
      },
      [&] { return nativeClassifyOriginal(candidate, view, position, skipDistanceBounds, bias); },
      [](const StereoNativeClassifyToken &token, uint32_t returned, uint32_t post) {
        stereo_audit_native_classify_end(token, returned, post);
      });
}
void nativeTransformHook(void* shader,void* view,void* mesh,bool alternateOwner,bool skipEyeConstants){
    const auto caller=reinterpret_cast<std::uintptr_t>(LMU_CALLER_ADDRESS);
    if(caller!=base+0x562acf){
        nativeTransformOriginal(shader,view,mesh,alternateOwner,skipEyeConstants);return;
    }
    SpsCpuScope cpuScope(!alternateOwner&&!skipEyeConstants?SpsCpuWork::transform:SpsCpuWork::other_transform);
    NativeStereoTransformArgs args{shader,view,mesh,address<void>(0x1b13630)};
    args.boundary=NativeStereoTransformBoundary::Helper;
    args.alternateOwner=alternateOwner;args.skipEyeConstants=skipEyeConstants;
    if(spsRenderingOptIn)nativeSpsRuntime.transform(args);
    invoke_native_stereo_transform_helper(
        [&](unsigned entry){return stereo_audit_native_transform_begin({args,readable},entry);},
        [&]{nativeTransformOriginal(shader,view,mesh,alternateOwner,skipEyeConstants);},
        [](const StereoNativeTransformToken& token,unsigned post){stereo_audit_native_transform_end(token,post);},stereo_audit_enabled());
}
int nativePrepareHook(void* shader,void* view,void* mesh,std::uint32_t flags,bool reset){
    const auto caller=reinterpret_cast<std::uintptr_t>(LMU_CALLER_ADDRESS);
    // Two verified world-sort submission sites. This hook observes the complete
    // original preparation and forwards its exact result; no setup is skipped.
    const bool known=caller==base+0x51f67e||caller==base+0x51f5c4;
    SpsCpuScope cpuScope(known?SpsCpuWork::preparation:SpsCpuWork::count);
    const StereoNativePrepareInfo preparation{shader, flags, reset, readable};
    const bool observeSps=spsRenderingOptIn&&nativeSpsRuntime.observing_group();
    if(observeSps)nativeSpsRuntime.preparation_begin(
        reinterpret_cast<uintptr_t>(shader),reinterpret_cast<uintptr_t>(view),reinterpret_cast<uintptr_t>(mesh),flags,reset,known?uint32_t(caller-base):0);
    const int returned=invoke_native_stereo_transform(
        [&](unsigned){return known?stereo_audit_native_prepare_begin(mesh,&preparation):StereoNativePrepareToken{};},
        [&]{return nativePrepareOriginal(shader,view,mesh,flags,reset);},
        [](const StereoNativePrepareToken& token,unsigned){stereo_audit_native_prepare_end(token);},stereo_audit_enabled());
    if(observeSps)nativeSpsRuntime.preparation_end(returned);
    return returned;
}
void nativeUploadHook(void* cache,void* device,ID3D11DeviceContext* context,void* bindings){
    const auto caller=reinterpret_cast<std::uintptr_t>(LMU_CALLER_ADDRESS);
    SpsCpuScope cpuScope(caller==base+0x5b6034?SpsCpuWork::upload:SpsCpuWork::count);
    if(!selectedUploadCache){nativeUploadOriginal(cache,device,context,bindings);return;}
    // Keep bindings/bookkeeping and every changed upload in the native routine.
    // Unexpected native writers end reuse for the rest of this group.
    if(caller!=base+0x5b6034||cache!=selectedUploadCache||context!=selectedUploadContext){nativeUploadBatch.end();nativeUploadReuse.end();nativeUploadOriginal(cache,device,context,bindings);return;}
    const auto token=nativeUploadReuse.before(cache,context);
    if(nativeUploadBatchOptIn){const auto result=nativeUploadBatch.run(cache,device,context,bindings,nativeUploadOriginal);nativeUploadReuse.after(cache,context,token);
        if(nativeUploadBatch.failure_serial()!=batchFailureLogged){batchFailureLogged=nativeUploadBatch.failure_serial();if(diagnostics_enabled()&&batchFailureMessages++<8){char line[256]{};std::snprintf(line,sizeof(line),"cpu_upload_batch fallback reason=%s range=%llu hr=%08lx; native upload path restored",nativeUploadBatch.failure_reason(),static_cast<unsigned long long>(nativeUploadBatch.failure_range()),static_cast<unsigned long>(nativeUploadBatch.failure_hr()));log_line(line);}}
        // Keep the game's upload statistic accurate for our actual Map calls.
        *address<std::uint32_t>(0x1b1a55c)+=result.maps;return;}
    nativeUploadOriginal(cache,device,context,bindings);nativeUploadReuse.after(cache,context,token);
}
// Keep large copied objects and their stack-cookie cleanup out of the thin
// forwarding hook. All producer cleanup/FP restoration precedes native entry.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void captureNativeOwnerInputs(void* owner,void* pointer,void* buckets) noexcept {
    {
        NativeStereoFpGuard fp;
        try {
            auto* view=static_cast<ViewSnapshot*>(pointer);
            if(readable(view,sizeof(*view))&&view->eye_index==0&&view->parent==0&&
               readable(address<void>(0x1a51370),sizeof(void*))&&*address<void*>(0x1a51370)==pointer&&
               view->scene<=(std::numeric_limits<uintptr_t>::max)()-0x70&&
               reinterpret_cast<uintptr_t>(buckets)==view->scene+0x70){
                uint32_t mask=0,globalMask=0;
                std::memcpy(&mask,reinterpret_cast<const std::byte*>(view)+0x10c,4);
                if(readable(address<void>(0x1b12c28),4)){
                    std::memcpy(&globalMask,address<void>(0x1b12c28),4);
                    std::lock_guard<std::mutex> lock(spsOwnerMutex);
                    if(nativeOwnerInputEnabled.load(std::memory_order_relaxed)&&
                       spsOwnerFrame.accepts(spsOwnerFrameIdentity,view->scene,reinterpret_cast<uintptr_t>(view)))
                        spsOwnerFrame.append(spsOwnerFrameIdentity,view->scene,reinterpret_cast<uintptr_t>(view),
                            {mask,true},owner,globalMask,readable);
                }
            }
        } catch(...) {try {std::lock_guard<std::mutex> lock(spsOwnerMutex);spsOwnerFrame.reset();}catch(...) {}}
    }
}
void nativeOwnerHook(void* owner,void* pointer,void* buckets,float bias){
    const auto caller=reinterpret_cast<uintptr_t>(LMU_CALLER_ADDRESS);
    const bool known=caller==base+0x5def20||caller==base+0x5df0b2||caller==base+0x5dfe2c||caller==base+0x5dffeb;
    // Recursive entries and unrelated callers retain the original fast path.
    if(known&&nativeOwnerInputEnabled.load(std::memory_order_relaxed)&&insideHook&&pointer==focusView&&
       spsOwnerFrameIdentity.valid()&&spsOwnerFrameIdentity.thread==GetCurrentThreadId())
        captureNativeOwnerInputs(owner,pointer,buckets);
    nativeOwnerOriginal(owner,pointer,buckets,bias);
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
    const auto caller=reinterpret_cast<std::uintptr_t>(LMU_CALLER_ADDRESS);
    if(!nativeOuterOptIn||nativeOuterFailed||insideHook||view!=nativeOuterExpected||
       caller!=base+0x5de951||!readable(view,sizeof(*view))||view->eye_index<0||view->eye_index>1){
        nativeViewOriginal(pointer,objects,lights,main,later);return;
    }
    if(!benchmarkPath.empty() && benchmarkRequestedRate()!=2){
        nativeViewOriginal(pointer,objects,lights,main,later);
        ++nativeOuterFull[view->eye_index];return;
    }
    // Loading/standby frames cannot establish a usable resource bank. This
    // read-only query also requires current located views and matching time;
    // unlike xr_frame_views it does not lock the submission's view snapshot.
    XrFovf renderableFov[2]{};
    if(!xr_frame_full_outer_fov(renderableFov)){
        nativeViewOriginal(pointer,objects,lights,main,later);return;
    }
    // VR wrapper associations initially reference desktop depth targets. Native
    // main-view setup replaces those during the first ordinary scene renders.
    if(view!=nativeOuterWarmupView){nativeOuterWarmupView=view;nativeOuterWarmupCalls=0;}
    if(nativeOuterWarmupCalls<120){
        ++nativeOuterWarmupCalls;
        nativeViewOriginal(pointer,objects,lights,main,later);
        ++nativeOuterFull[view->eye_index];return;
    }
    if(nativeOuterRetryCalls){
        --nativeOuterRetryCalls;
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
    ProfileScope prepareProfile(profile_eye(ProfileSlot::native_prepare0,unsigned(view->eye_index)));
    const char* reason=nullptr;
    const float renderScale=*address<float>(0x199cc18);
    if(!readable(reinterpret_cast<void*>(renderer),0x82cd8))reason="renderer_unavailable";
    else if(*reinterpret_cast<bool*>(renderer+0x76f50))reason="renderer_mode";
    else if(renderScale!=1.f)reason="render_scale";
    else if(view->flags&0xc0000000u)reason="view_flags";
    const auto passes=view->main_passes;
    if(!reason&&(passes.end<passes.begin||passes.end-passes.begin!=0x94))reason="main_pass_layout";
    if(!reason&&!readable(reinterpret_cast<void*>(passes.begin),0x94))reason="main_pass_unavailable";
    if(!reason&&*reinterpret_cast<bool*>(passes.begin))reason="main_pass_mode";
    const float clearDepth=*reinterpret_cast<unsigned*>(reinterpret_cast<std::byte*>(view)+0x3e8)==1?0.f:1.f;
    if(!reason&&!nativeOuterBank.prepare(base,renderer,width,height,clearDepth,readable,log_line,nativeOuterSsrOptIn))reason="resource_bank";
    if(reason){
        // Bound allocation attempts across the session; normal full-resolution
        // quad rendering continues while resources settle and after exhaustion.
        nativeOuterFailed=++nativeOuterPrepareFailures>=3;
        nativeOuterRetryCalls=nativeOuterFailed?0:120;
        char message[512]{};std::snprintf(message,sizeof(message),
            "native_outer refused reason=%s renderScale=%.6g flags=0x%08x passBytes=%llu attempt=%u/3 retryAfter=%u renderableViews; live quad fallback: full-resolution outer render resampled to runtime output; outer render savings unavailable%s",
            reason,double(renderScale),view->flags,static_cast<unsigned long long>(passes.end>=passes.begin?passes.end-passes.begin:0),
            nativeOuterPrepareFailures,nativeOuterRetryCalls,renderScale!=1.f?"; select LMU Bilinear (render scale 1) for smaller outer targets":"");
        log_line(message);
        prepareProfile.end();nativeViewOriginal(pointer,objects,lights,main,later);return;
    }
    if(nativeOuterSsrOptIn&&!nativeOuterBank.ssrActive()){
        nativeOuterSsrOptIn=false;
        log_line("native_outer SSR unavailable for this session; primary small bank retained with original SSR resources");
    }
    NativeOuterBindings bindings;
    void* target=nullptr;void* second=nullptr;
    bool valid=nativeOuterBank.mapTarget(reinterpret_cast<void*>(view->render_target),target)&&
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
        prepareProfile.end();nativeViewOriginal(pointer,objects,lights,main,later);return;
    }
    const auto savedProjection=view->projection,savedFar=view->far_projection;
    const auto savedFlags=view->flags;
    nativeOuterActive=view;nativeOuterProjection=&savedProjection;nativeOuterBindings=&bindings;
    prepareProfile.end();
    nativeViewOriginal(pointer,objects,lights,main,later);
    ProfileScope restoreProfile(profile_eye(ProfileSlot::native_restore0,unsigned(view->eye_index)));
    nativeOuterBindings=nullptr;nativeOuterActive=nullptr;nativeOuterProjection=nullptr;
    nativeOuterSource=nativeOuterBank.final(unsigned(view->eye_index));
    bindings.restore();
    view->projection=savedProjection;view->far_projection=savedFar;view->flags=savedFlags;
    derivedOriginal(view,6,false);
    view->flags=savedFlags;
    reinterpret_cast<void(*)(void*,void*)>(base+0x5c6c50)(reinterpret_cast<void*>(renderer),view);
    restoreProfile.end();
    const auto count=++nativeOuterCompleted[view->eye_index];
    if(count<=4||count%600==0){
        char line[240]{};std::snprintf(line,sizeof(line),"native_outer rendered eye=%d count=%llu actual=%ux%u focus=%ux%u restored=1; PPFX pool remains full-size",
            view->eye_index,static_cast<unsigned long long>(count),width,height,fullW,fullH);log_line(line);
    }
}
void renderHook(void* unknown, bool laterPass) {
    suspendUnexpectedVrs();
    const auto caller=reinterpret_cast<std::uintptr_t>(LMU_CALLER_ADDRESS);
    ID3D11DeviceContext* profileContext=nullptr;
    ViewSnapshot* auditExpectedView=nullptr;
    unsigned profileEye=0;
    if((renderProfile.enabled()||stereo_audit_enabled()||spsCpuProfile.enabled())&&!insideHook&&installed.load()&&caller==base+0xcd1763&&(xr_quad_active()||xr_vrs_active())){
        auto* before=*address<ViewSnapshot*>(0x1a51370);
        const auto renderer=*address<std::uintptr_t>(0x1b12be8);
        if(readable(before,sizeof(*before))&&before->eye_index>=0&&before->eye_index<=1&&
           readable(reinterpret_cast<void*>(renderer+0x76f68),sizeof(void*))){
            auto* context=*reinterpret_cast<ID3D11DeviceContext**>(renderer+0x76f68);
            if(readable(context,sizeof(void*))){
                profileContext=context;profileEye=unsigned(before->eye_index);
                auditExpectedView=before;
                if(profileEye==0){
                    const bool cpuKey=(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_MENU)&0x8000)&&(GetAsyncKeyState(VK_F10)&0x8000);
                    spsCpuProfile.frame(cpuKey);
                    const bool key=(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_MENU)&0x8000)&&(GetAsyncKeyState(VK_F9)&0x8000);
                    renderProfile.frame(context,key);
                    const bool auditKey=(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_MENU)&0x8000)&&(GetAsyncKeyState(VK_F8)&0x8000);
                    stereo_audit_frame(auditKey);
                }
            }
        }
    }
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
       (xr_quad_active()||xr_vrs_active()) && caller==base+0xcd1763) {
        auto* before=*address<ViewSnapshot*>(0x1a51370);
        const auto renderer=*address<std::uintptr_t>(0x1b12be8);
        if(readable(before,sizeof(*before)) && before->eye_index>=0 && before->eye_index<=1 &&
           (before->flags&0xc0000000u)==0 &&
           readable(reinterpret_cast<void*>(renderer+0x76f68),sizeof(void*))) {
            auto* context=*reinterpret_cast<ID3D11DeviceContext**>(renderer+0x76f68);
            if(readable(context,sizeof(void*))) {
                bool prepared=true;
                if(!benchmark && (stereoVrsMode||before->eye_index==0))prepared=prepareWorldVrs(context,before);
                if(prepared && worldVrs.ready() && !worldVrsFailed && context==worldVrsConfiguredContext) {
                    selectedOuterView=before;outerVrsContext=context;
                }
            }
        }
    }
    auto* savedNativeExpected=nativeOuterExpected;
    auto* savedNativeSource=nativeOuterSource;
    nativeOuterExpected=nullptr;nativeOuterSource=nullptr;
    if(((nativeOuterOptIn&&!nativeOuterFailed)||nativeUploadBatchOptIn||nativeUploadOuterOptIn)&&!insideHook&&installed.load()&&xr_quad_active()&&caller==base+0xcd1763){
        auto* expected=*address<ViewSnapshot*>(0x1a51370);
        if(readable(expected,sizeof(*expected))&&expected->eye_index>=0&&expected->eye_index<=1)
            nativeOuterExpected=expected;
    }
    const bool profileOuterGpu=profileContext&&renderProfile.gpu_start(profileContext,profileEye);
    ProfileScope outerProfile(profile_eye(ProfileSlot::outer0,profileEye),profileContext!=nullptr);
    StereoAuditScope outerAudit(profileContext?stereo_audit_eye(false,profileEye):StereoAuditPass::none,auditExpectedView);
    {SpsCpuPassScope cpuPass(profileContext?profileEye:4,auditExpectedView);renderOriginal(unknown,laterPass);}
    if(stereoVrsMode&&!insideHook&&caller==base+0xcd1763){
        auto* renderedView=*address<ViewSnapshot*>(0x1a51370);
        if(readable(renderedView,sizeof(*renderedView))&&renderedView->eye_index>=0&&renderedView->eye_index<=1)
            xr_vrs_scene_complete(unsigned(renderedView->eye_index));
    }
    outerAudit.end();
    outerProfile.end();
    if(profileOuterGpu)renderProfile.gpu_stop(profileContext);
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
    QuadFrameInputs frameInputs;
    const bool ownedFrame=spsPreparationOptIn&&xr_frame_inputs(frameInputs);
    if(ownedFrame)std::copy(frameInputs.views.begin(),frameInputs.views.end(),std::begin(views));
    else {if(spsPreparationOptIn)spsFocusFrame.reset();if(!xr_frame_views(views))return;}
    auto* view=*address<ViewSnapshot*>(0x1a51370);
    if(!readable(view,sizeof(*view)) || view->eye_index<0 || view->eye_index>1) return;
    const unsigned eye=unsigned(view->eye_index);
    ProfileScope focusHookProfile(profile_eye(ProfileSlot::focus_hook0,eye));
    if(view->flags & 0xc0000000u) {
        log_line("engine: focus refused for alternative/oblique view projection");return;
    }
    auto scene=*address<void*>(0x3b4eb88);
    if(!readable(scene,sizeof(void*))) return;
    if(!equalPose(views[eye].pose,views[eye+2].pose)) { log_line("engine: outer/focus pose mismatch");return; }
    auto* source=smallOuterSource?smallOuterSource:*address<ID3D11Texture2D*>(0x1b1c6b8+eye*8);
    if(!readable(source,sizeof(void*))) return;
    auto calls=++renderCalls;
    if(diagnostics_enabled()&&(calls<=8 || calls%600==0)) {
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        char msg[400];std::snprintf(msg,sizeof(msg),"engine outer eye=%u call=%llu view=%p texture=%p size=%ux%u samples=%u flags=%x",
            eye,static_cast<unsigned long long>(calls),static_cast<void*>(view),static_cast<void*>(source),
            desc.Width,desc.Height,desc.SampleDesc.Count,view->flags);log_line(msg);
        const auto renderer=*address<std::uintptr_t>(0x1b12be8);
        auto* context=readable(reinterpret_cast<void*>(renderer+0x76f68),sizeof(void*))?*reinterpret_cast<ID3D11DeviceContext**>(renderer+0x76f68):nullptr;
        diagnoseNativeEye(smallOuterSource?"small-outer-scene":"native-final-outer-scene",eye,source,
            *address<ID3D11RenderTargetView*>(0x1b1c6c8+eye*8),*address<ID3D11ShaderResourceView*>(0x1b1c6d8+eye*8),context);
    }
    float nearZ{},farZ{};
    std::memcpy(&nearZ,reinterpret_cast<std::byte*>(view)+0x24,4);
    std::memcpy(&farZ,reinterpret_cast<std::byte*>(view)+0x28,4);
    Matrix inner{},innerFar{};
    if(spsPreparationOptIn&&eye==0){
        NativeStereoFpGuard guard;
        spsFocusFrame.reset();float scaleX{},scaleY{};
        spsViewInputFrame.reset();
        const auto camera=ownedFrame?capture_native_stereo_camera(view,address<void>(0x1b1be30),
            address<void>(0x1a51400),address<void>(0x1a51370),readable):NativeStereoCameraSnapshot{};
        const auto worldPose=!spsNativePoseRejected&&nativeCameraStateHooked&&nativeCameraPoseHooked&&
            spsWorldPoseEpoch==spsNativePoseEpoch.load()?spsWorldPoseFrame.take(reinterpret_cast<uintptr_t>(view),camera):NativeStereoWorldPosePair{};
        spsExpectedWorldPose=worldPose;spsExpectedPoseView=worldPose.complete?reinterpret_cast<uintptr_t>(view):0;
        spsExpectedPoseIdentity=worldPose.complete?frameInputs.identity:StereoFrameIdentity{};
        if(worldPose.complete){++spsNativePoseProduced;if(!spsNativePoseLogged){
            log_line("sps_prepare: native cockpit world-pose inputs connected; left position checked bit-for-bit; right view built directly from shared camera inputs; in-game right-eye equivalence remains to be checked");spsNativePoseLogged=true;}}
        else if(++spsNativePoseFallback==60){char line[640]{};
            const auto* reason=spsWorldPoseEpoch==spsNativePoseEpoch.load()?spsNativePoseSourceReason:"camera-state boundary not captured on render thread";
            uintptr_t renderer=0,table=0,getter=0;
            if(readable(address<void>(0x1b12be8),8)){std::memcpy(&renderer,address<void>(0x1b12be8),8);
                if(readable(reinterpret_cast<void*>(renderer),8)){std::memcpy(&table,reinterpret_cast<void*>(renderer),8);
                    if(table<=UINTPTR_MAX-0x7c8&&readable(reinterpret_cast<void*>(table+0x7c8),8))std::memcpy(&getter,reinterpret_cast<void*>(table+0x7c8),8);}}
            const auto rva=[](uintptr_t p){return p>=base&&p-base<=UINT32_MAX?unsigned(p-base):0u;};
            std::snprintf(line,sizeof(line),"sps_prepare world-pose unavailable reason=%s camera_complete=%d camera_mode=%d transform_mode=%d offsets=%.7g/%.7g/%.7g/%.7g/%.7g/%.7g state_caller=%x pose_caller=%x state_getter=%x; native rendering retained",
                reason,int(camera.complete),camera.cameraMode,camera.transformMode,camera.translation[0],camera.translation[1],camera.translation[2],camera.euler[0],camera.euler[1],camera.euler[2],rva(spsNativeStateCaller),rva(spsNativePoseCaller),rva(getter));log_line(line);}
        const bool prepared=ownedFrame&&projectionScales(scaleX,scaleY)&&
            spsFocusFrame.prepare(frameInputs,camera,reinterpret_cast<uintptr_t>(scene),
                reinterpret_cast<uintptr_t>(view),nearZ,farZ,scaleX,scaleY,worldPose.complete?&worldPose:nullptr);
        if(prepared&&!spsNativeViewRejected){
            const auto* focus=spsFocusFrame.inputs(frameInputs.identity);
            if(focus&&spsViewInputFrame.prepare(*focus,worldPose,reinterpret_cast<uintptr_t>(scene),reinterpret_cast<uintptr_t>(view)))
                ++spsNativeViewProduced;
            else if(++spsNativeViewRefused==60){char line[256]{};
                std::snprintf(line,sizeof(line),"sps_prepare view producer unavailable reason=%s; native rendering retained",spsViewInputFrame.reason());log_line(line);}
        }
        if(prepared){++spsPreparationCounts.prepared;if(!spsPreparedLogged){
            log_line("sps_prepare: both focus cameras owned before left focus; current locked XR FOVs; native pose used when available; native eye draws retained");spsPreparedLogged=true;}}
        else {++spsPreparationCounts.refused;if(!spsPreparationRefusedLogged){char line[256]{};
            std::snprintf(line,sizeof(line),"sps_prepare fallback: %s; ordinary per-eye preparation retained",ownedFrame?spsFocusFrame.reason():"rendered frame identity unavailable");
            log_line(line);spsPreparationRefusedLogged=true;}}
    }
    const bool preparedProjection=ownedFrame&&spsFocusFrame.consume(eye,frameInputs.identity,
        reinterpret_cast<uintptr_t>(scene),reinterpret_cast<uintptr_t>(view),view->projection,view->far_projection,inner,innerFar);
    if(spsPreparationOptIn){if(preparedProjection)++spsPreparationCounts.used[eye];else ++spsPreparationCounts.fallback[eye];}
    if(!preparedProjection&&(!projection(views[eye+2],nearZ,farZ,inner) || !projection(views[eye+2],nearZ,1000000,innerFar))) {
        log_line("engine: invalid focus projection parameters");return;
    }
    if(view->projection[11]!=1.f || view->projection[15]!=0.f) {
        log_line("engine: unsupported native perspective convention");return;
    }
    inner=replace_fov(view->projection,inner);
    innerFar=replace_fov(view->far_projection,innerFar);
    bool copied=false;
    {ProfileScope copyProfile(profile_eye(ProfileSlot::outer_copy0,eye));copied=xr_copy_outer(eye,source);}
    if(!copied)return;
    ProfileScope focusPrepareProfile(profile_eye(ProfileSlot::focus_prepare0,eye));
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
    if(spsPreparationOptIn&&ownedFrame&&!spsNativeViewRejected&&spsViewInputFrame.accepts(frameInputs.identity)){
        const auto observed=capture_native_stereo_view_observation(frameInputs.identity,reinterpret_cast<uintptr_t>(scene),view,readable);
        if(spsViewInputFrame.observe(observed)){
            ++spsNativeViewMatches[eye];
            if(eye==1&&!spsNativeViewsLogged){
                log_line("sps_prepare: both native focus views match owned camera/constant/classifier/bounds positions, inverse matrices, planes and common controls; scene/draw effects remain native");
                spsNativeViewsLogged=true;
            }
        }else{
            ++spsNativeViewRefused;spsNativeViewRejected=true;
            if(!spsNativeViewsRefusalLogged){char line[288]{};std::snprintf(line,sizeof(line),
                "sps_prepare view input fallback: %s; current pair refused; original traversal/draws retained",spsViewInputFrame.reason());
                log_line(line);spsNativeViewsRefusalLogged=true;}
        }
    }
    bool captureOwnerFrame=false;
    if(eye==0&&nativeOwnerInputEnabled.load(std::memory_order_relaxed)){
        std::lock_guard<std::mutex> lock(spsOwnerMutex);spsOwnerFrameIdentity={};spsOwnerFrame.reset();
        if(ownedFrame&&spsOwnerCapture.begin(frameInputs.identity)){captureOwnerFrame=true;
            uint32_t mask=0;std::memcpy(&mask,reinterpret_cast<const std::byte*>(view)+0x10c,4);
            spsOwnerFrameIdentity=frameInputs.identity;spsOwnerFrame.begin(frameInputs.identity,
                view->scene,reinterpret_cast<uintptr_t>(view),{mask,view->parent==0});}
    }
    focusPrepareProfile.end();
    const bool measureFocus=timingContext&&(!benchmark||benchmarkControl.state()==BenchmarkControl::State::measuring);
    const bool focusTimed=measureFocus && gpuTiming.start(timingContext,eye+2);
    const bool focusCounted=measureFocus&&benchmark&&pipelineStats.start(timingContext,eye+2);
    const bool profileFocusGpu=profileContext&&renderProfile.gpu_start(profileContext,eye+2);
    ProfileScope focusSceneProfile(profile_eye(ProfileSlot::focus_scene0,eye));
    StereoAuditScope focusAudit(stereo_audit_eye(true,eye),view);
    {SpsRenderFrameScope spsFrame(spsRenderingOptIn&&ownedFrame?frameInputs.identity:StereoFrameIdentity{});
     SpsCpuPassScope cpuPass(eye+2,view);reinterpret_cast<SceneFn>(base+0x5de200)(scene,false,true);}
    if(spsRenderingOptIn&&eye==1&&spsPreparationCounts.prepared>=120)nativeSpsRuntime.report_progress_once();
    if(captureOwnerFrame){
        NativeStereoFpGuard fp;std::lock_guard<std::mutex> lock(spsOwnerMutex);
        if(spsOwnerFrame.seal(frameInputs.identity)){
            ++spsOwnerFrames;spsOwnerRoots+=spsOwnerFrame.root_count();
            spsOwnerAdmissionReadyRoots+=spsOwnerFrame.admission_ready_roots();
            spsOwnerWorldRefreshOwners+=spsOwnerFrame.world_refresh_owners();
            spsOwnerBoundsAdmissionOwners+=spsOwnerFrame.bounds_admission_owners();
            if(!spsOwnerReadyLogged){char line[384]{};std::snprintf(line,sizeof(line),
                "sps_prepare: selected-root descriptions sealed roots=%zu admission_ready_roots=%zu world_refresh_owners=%zu bounds_admission_owners=%zu; pending native effects retained; no draw eligibility",
                spsOwnerFrame.root_count(),spsOwnerFrame.admission_ready_roots(),spsOwnerFrame.world_refresh_owners(),spsOwnerFrame.bounds_admission_owners());
                log_line(line);spsOwnerReadyLogged=true;}
        }else {++spsOwnerRefusals;if(!spsOwnerRefusalLogged){char line[256]{};
            std::snprintf(line,sizeof(line),"sps_prepare owner inputs unavailable: %s; native traversal retained",spsOwnerFrame.reason());
            log_line(line);spsOwnerRefusalLogged=true;}}
        spsOwnerFrameIdentity={};
        nativeOwnerInputEnabled.store(false);
        log_line("sps_prepare owner capture finished: single-frame request consumed; continuous collection disabled");
    }
    focusAudit.end();
    focusSceneProfile.end();
    if(profileFocusGpu)renderProfile.gpu_stop(profileContext);
    if(focusCounted) pipelineStats.stop(timingContext);
    if(focusTimed) gpuTiming.stop(timingContext);
    ProfileScope focusRestoreProfile(profile_eye(ProfileSlot::focus_restore0,eye));
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
    focusRestoreProfile.end();
    auto rendered=++focusCalls;
    if(nativeUploadBatchOptIn&&diagnostics_enabled()&&rendered%1200==0){logUploadBatchStats("checkpoint");flush_log();}
    if(diagnostics_enabled()&&(rendered<=8 || rendered%600==0)) {
        char msg[256];std::snprintf(msg,sizeof(msg),"engine focus eye=%u count=%llu proj00=%.5f outer00=%.5f overrides=%u masksSkipped=%u",
            eye,static_cast<unsigned long long>(rendered),inner[0],outerProjection[0],projectionOverrides,skippedMasks);log_line(msg);
    }
}
void uiHook(void* object,ID3D11DeviceContext* context,const UiArgs* originalArgs) {
    ProfileScope uiProfile(ProfileSlot::ui);
    XrView views[4]{};
    if(!xr_quad_active() || !xr_frame_views(views) || !readable(originalArgs,sizeof(UiArgs)) ||
       !readable(object,0xe8) || originalArgs->mode<1 || originalArgs->mode>3) {
        uiOriginal(object,context,originalArgs);return;
    }
    const bool menu=originalArgs->mode==3 && !xr_outer_rtv(0) && !xr_outer_rtv(1);
    auto targetSlots=reinterpret_cast<ID3D11RenderTargetView**>(static_cast<std::byte*>(object)+0xd8);
    static uint64_t diagnostics=0;
    if(diagnostics_enabled()&&(++diagnostics<=4||diagnostics%600==0)){
        for(unsigned eye=0;eye<2;++eye)diagnoseNativeEye(menu?"menu-final-before-ui":"scene-focus-before-ui",eye,
            *address<ID3D11Texture2D*>(0x1b1c6b8+eye*8),targetSlots[eye],*address<ID3D11ShaderResourceView*>(0x1b1c6d8+eye*8),context);
        char line[200]{};std::snprintf(line,sizeof(line),"native final extent=%ux%u copyOffsets left=%u,%u right=%u,%u",
            *address<unsigned>(0x1b1c654),*address<unsigned>(0x1b1c658),*address<unsigned>(0x1b1c644),*address<unsigned>(0x1b1c64c),*address<unsigned>(0x1b1c648),*address<unsigned>(0x1b1c650));log_line(line);
        if(nativeRuntimeSettings||nativeCropWidth.percent!=100||nativeCropHeight.percent!=100){
            const auto* camera=address<lmu::reconstructed::VrCameraState>(0x1b1be30);
            if(readable(camera,sizeof(*camera)))for(unsigned eye=0;eye<2;++eye){
                Matrix cropped{};
                if(!projection(views[eye],.1f,1000,cropped))continue;
                const auto& native=camera->engine_projection[eye].matrix;
                float uiDifference=0;
                for(unsigned j=0;j<16;++j)uiDifference=(std::max)(uiDifference,
                    std::abs(originalArgs->transform[eye].matrix[j]-camera->ui_composite[eye].matrix[j]));
                char detail[512]{};std::snprintf(detail,sizeof(detail),
                    "crop UI projection eye=%u mode=%u native=[%.7g %.7g %.7g %.7g %.7g] render=[%.7g %.7g %.7g %.7g %.7g] argsCameraMaxDiff=%.7g",
                    eye,originalArgs->mode,native[0],native[5],native[8],native[9],native[11],
                    cropped[0],cropped[5],cropped[8],cropped[9],cropped[11],uiDifference);log_line(detail);
            }
            if(readable(object,0x108)){
                auto value=[&](size_t offset){float v{};std::memcpy(&v,static_cast<const std::byte*>(object)+offset,sizeof(v));return v;};
                char detail[240]{};std::snprintf(detail,sizeof(detail),
                    "crop UI geometry mode=%u sourceSize=%.7g/%.7g planeFov=%.7g menuScale=%.7g/%.7g hudScale=%.7g/%.7g",
                    originalArgs->mode,value(0x88),value(0x8c),value(0xc4),value(0xf8),value(0xfc),value(0x100),value(0x104));log_line(detail);
            }
        }
    }
    XrFovf fullOuterFov[2]{};
    float planeScaleFactor=1.f;
    void* planeScale=nullptr;
    if(readable(object,0x108)&&xr_frame_full_outer_fov(fullOuterFov)){
        auto* bytes=static_cast<std::byte*>(object);
        float planeFov{};std::memcpy(&planeFov,bytes+0xc4,sizeof(planeFov));
        planeScaleFactor=ui_plane_scale_factor(planeFov,
            views[0].fov.angleUp-views[0].fov.angleDown,
            fullOuterFov[0].angleUp-fullOuterFov[0].angleDown);
        planeScale=bytes+(originalArgs->mode==1?0x100:0xf8);
        if(diagnostics_enabled()&&(diagnostics<=4||diagnostics%600==0)){
            char detail[200]{};std::snprintf(detail,sizeof(detail),
                "crop UI size mode=%u planeFov=%.7g fullSpan=%.7g xyScaleFactor=%.7g; native depth and curvature retained",
                originalArgs->mode,planeFov,fullOuterFov[0].angleUp-fullOuterFov[0].angleDown,planeScaleFactor);log_line(detail);
        }
    }
    // Apply to both focus and peripheral draws, including fallback calls. The
    // native c4 extent and depth stay intact so the baked mesh keeps its curve.
    UiPlaneScaleScope planeSize(planeScale,planeScaleFactor);
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
    // Outer capture already validated these dimensions. Avoid COM descriptor
    // queries per eye and preserve state only when adding an outer UI draw.
    const OuterTarget outerTargets[]{xr_outer_target(0), xr_outer_target(1)};
    if (outerTargets[0].rtv || outerTargets[1].rtv) {
        UiD3dState restoreState(context);
        auto drawEye = reinterpret_cast<UiEyeFn>(base + 0x617fb0);
        for (unsigned eye = 0; eye < 2; ++eye) {
            const auto& target = outerTargets[eye];
            if (!target.rtv) continue;
            UiTargetScope targetScope(targetSlots[eye],
                reinterpret_cast<UINT*>(static_cast<std::byte*>(object) + 0xbc),
                target.rtv, target.width, target.height);
            drawEye(object, context, originalArgs, eye);
        }
    }
    auto calls=++uiCalls;
    if(diagnostics_enabled()&&(calls<=8 || calls%600==0)) {
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
    {fovScaleRva,reinterpret_cast<void*>(&fovScaleHook),reinterpret_cast<void**>(&fovScaleOriginal)},
    {0x8c2d30,reinterpret_cast<void*>(&nativeEnumerateHook),reinterpret_cast<void**>(&nativeEnumerateOriginal)},
    {0x51d8a0,reinterpret_cast<void*>(&mainPassHook),reinterpret_cast<void**>(&mainPassOriginal)},
    {0x51f040,reinterpret_cast<void*>(&worldSortHook),reinterpret_cast<void**>(&worldSortOriginal)},
    {0x5df6f0,reinterpret_cast<void*>(&nativeViewHook),reinterpret_cast<void**>(&nativeViewOriginal)}
};
constexpr std::size_t fovScaleHookIndex=5;
static_assert(sizeof(hooks)/sizeof(hooks[0])>fovScaleHookIndex);
// 0.3.3: only hooks whose code anchor was verified for this launch are installed.
// The list records exactly what was patched so teardown removes the same set.
std::array<std::size_t,sizeof(hooks)/sizeof(hooks[0])> selectedHooks{};
void removeHooks(std::size_t count) {
    for(std::size_t i=0;i<count;++i) MH_DisableHook(address<void>(hooks[selectedHooks[i]].rva));
    for(std::size_t i=0;i<count;++i) MH_RemoveHook(address<void>(hooks[selectedHooks[i]].rva));
}
// 0.3.3: the executable hash is no longer an allowlist, so short prologue anchors
// alone could match an unrelated function after a game update. A hook target must
// lie in an executable section and must not fall inside a framed function's range
// unless it is exactly that function's start (x64 exception directory). Only
// two known leaf functions may lack .pdata; their complete bodies must match.
bool knownLeafTarget(std::uintptr_t rva) {
    return (rva==0x557810&&matches(rva,projection_leaf_anchor))||
        (rva==nativeCameraStateRva&&matches(rva,nativeCameraStateAnchor))||
        (rva==fovScaleRva&&matches(rva,fovScaleAnchor));
}
bool hookTargetPlausible(std::uintptr_t rva) {
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(!readable(dos,sizeof(*dos))||dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>0x100000)return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+std::uintptr_t(dos->e_lfanew));
    if(!readable(nt,sizeof(*nt))||nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt->FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64))return false;
    const auto imageSize=nt->OptionalHeader.SizeOfImage;
    if(rva>=imageSize||static_cast<std::uint32_t>(dos->e_lfanew)>imageSize||sizeof(*nt)>imageSize-static_cast<std::uint32_t>(dos->e_lfanew))return false;
    const auto* sections=IMAGE_FIRST_SECTION(nt);const unsigned sectionCount=nt->FileHeader.NumberOfSections;
    if(!sectionCount||sectionCount>96||!readable(sections,sizeof(IMAGE_SECTION_HEADER)*sectionCount))return false;
    bool executable=false;
    for(unsigned i=0;i<sectionCount;++i){
        const auto begin=std::uintptr_t(sections[i].VirtualAddress);
        const auto size=std::uintptr_t((std::max)(sections[i].Misc.VirtualSize,sections[i].SizeOfRawData));
        if(rva>=begin&&rva-begin<size&&(sections[i].Characteristics&IMAGE_SCN_MEM_EXECUTE))executable=true;
    }
    if(!executable)return false;
    if(nt->OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_EXCEPTION)return false;
    const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    const std::size_t count=directory.Size/sizeof(RUNTIME_FUNCTION);
    if(!directory.VirtualAddress||!count||directory.Size%sizeof(RUNTIME_FUNCTION)||directory.VirtualAddress>=imageSize||directory.Size>imageSize-directory.VirtualAddress)return false;
    const auto* functions=reinterpret_cast<const RUNTIME_FUNCTION*>(base+directory.VirtualAddress);
    if(!readable(functions,count*sizeof(RUNTIME_FUNCTION)))return false;
    std::size_t low=0,high=count; // first entry whose BeginAddress > rva
    while(low<high){const auto mid=low+(high-low)/2;if(functions[mid].BeginAddress<=rva)low=mid+1;else high=mid;}
    if(low!=0){
        const auto& candidate=functions[low-1];
        if(candidate.BeginAddress>=candidate.EndAddress||candidate.EndAddress>imageSize)return false;
        if(rva<candidate.EndAddress)return rva==candidate.BeginAddress;
    }
    return knownLeafTarget(rva);
}
bool nativeClassifyTargetVerified() {
    const std::array<uint8_t,9> call{0xe8,0xec,0x3d,0x11,0x00,0x0f,0xba,0xe0,0x11};
    return matches(nativeClassifyRva,nativeClassifyAnchor)&&
        hookTargetPlausible(nativeClassifyRva)&&matches(0x41c8af,call);
}
bool nativeTransformTargetVerified() {
    const std::array<std::uint8_t,10> call{0xe8,0x91,0x27,0x00,0x00,0xf6,0xc3,0x08,0x74,0x27};
    return matches(nativeTransformRva,nativeTransformAnchor)&&
        hookTargetPlausible(nativeTransformRva)&&matches(0x562aca,call);
}
bool nativeOwnerTargetVerified(){
    const std::array<uint8_t,5> a{0xe8,0x20,0x22,0x00,0x00},b{0xe8,0x8e,0x20,0x00,0x00};
    const std::array<uint8_t,5> c{0xe8,0x14,0x13,0x00,0x00},d{0xe8,0x55,0x11,0x00,0x00};
    const std::array<uint8_t,20> masks{0x44,0x8b,0x41,0x74,0x45,0x85,0xc0,0x74,0x2c,0x48,0x85,0xd2,0x74,0x17,0x8b,0x82,0x0c,0x01,0x00,0x00};
    const std::array<uint8_t,5> source{0xe8,0x0a,0xfd,0xff,0xff};
    return matches(nativeOwnerRva,nativeOwnerAnchor)&&hookTargetPlausible(nativeOwnerRva)&&
        matches(0x5def1b,a)&&matches(0x5df0ad,b)&&matches(0x5dfe27,c)&&matches(0x5dffe6,d)&&
        matches(0x41a9a0,masks)&&matches(0x5e1201,source);
}
bool nativePoseTargetsVerified(){
    const std::array<uint8_t,25> frame{0x40,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8d,0xac,0x24,0xd0,0xfa,0xff,0xff,0x48,0x81,0xec,0x30,0x06,0x00,0x00};
    const std::array<uint8_t,11> stateCall{0xff,0x90,0xc8,0x07,0x00,0x00,0x45,0x85,0xe4,0x75,0x1b};
    const std::array<uint8_t,25> offsets{0xf3,0x0f,0x58,0xb5,0x4c,0x04,0x00,0x00,
        0xf3,0x0f,0x5c,0xbd,0x50,0x04,0x00,0x00,0xf3,0x44,0x0f,0x5c,0x85,0x54,0x04,0x00,0x00};
    const std::array<uint8_t,5> poseCall{0xe8,0x0d,0xab,0x8c,0xff};
    return matches(nativeCameraStateRva,nativeCameraStateAnchor)&&hookTargetPlausible(nativeCameraStateRva)&&
        matches(nativeCameraPoseRva,nativeCameraPoseAnchor)&&hookTargetPlausible(nativeCameraPoseRva)&&
        matches(0xc8b630,frame)&&hookTargetPlausible(0xc8b630)&&matches(0xc8c174,stateCall)&&
        matches(0xc8c17f,offsets)&&matches(0xc8c48e,poseCall);
}
bool nativeViewSettingsTargetsVerified(){
    const std::array<uint8_t,36> table{0xeb,0xb6,0xc8,0x00,0x0e,0xb7,0xc8,0x00,0x61,0xb7,0xc8,0x00,
        0x84,0xb7,0xc8,0x00,0xa7,0xb7,0xc8,0x00,0xd8,0xb7,0xc8,0x00,0xfb,0xb7,0xc8,0x00,
        0x1e,0xb8,0xc8,0x00,0xa5,0xb8,0xc8,0x00};
    const std::array<uint8_t,28> clip{0x83,0x3d,0x3d,0x50,0xdc,0x00,0x01,0xf3,0x0f,0x10,0xbe,0x80,0x00,0x00,0x00,
        0xf3,0x0f,0x10,0xb6,0x84,0x00,0x00,0x00,0xf3,0x0f,0x10,0x56,0x74};
    const std::array<uint8_t,16> nearCall{0xf3,0x0f,0x10,0x4e,0x78,0x45,0x33,0xc9,0x49,0x8b,0xce,0xe8,0xf9,0xa8,0x8c,0xff};
    const std::array<uint8_t,14> scale{0xf3,0x0f,0x10,0x46,0x7c,0xf3,0x41,0x0f,0x11,0x86,0x14,0x01,0x00,0x00};
    const std::array<uint8_t,26> range{0xf3,0x0f,0x10,0x4f,0x28,0xf3,0x0f,0x5c,0x4f,0x24,0x0f,0x28,0xc1,
        0xf3,0x0f,0x11,0x8f,0x00,0x01,0x00,0x00,0xf3,0x41,0x0f,0x59,0xc2};
    return matches(0xc8c8e8,table)&&matches(0xc8c3bc,clip)&&matches(0xc8c3f7,nearCall)&&
        matches(0xc8c447,scale)&&matches(0x556f6a,range);
}
void removeNativePoseHooks(){
    if(nativeCameraStateHooked){MH_DisableHook(address<void>(nativeCameraStateRva));MH_RemoveHook(address<void>(nativeCameraStateRva));nativeCameraStateHooked=false;}
    if(nativeCameraPoseHooked){MH_DisableHook(address<void>(nativeCameraPoseRva));MH_RemoveHook(address<void>(nativeCameraPoseRva));nativeCameraPoseHooked=false;}
    spsWorldPoseFrame.reset();spsExpectedWorldPose={};spsExpectedPoseView=0;spsAwaitingRightPose=false;spsWorldPoseEpoch=0;
}
bool installNativePoseHooks(){
    if(!nativePoseTargetsVerified())return false;
    if(MH_CreateHook(address<void>(nativeCameraStateRva),reinterpret_cast<void*>(&nativeCameraStateHook),
        reinterpret_cast<void**>(&nativeCameraStateOriginal))!=MH_OK)return false;
    nativeCameraStateHooked=true;
    if(MH_CreateHook(address<void>(nativeCameraPoseRva),reinterpret_cast<void*>(&nativeCameraPoseHook),
        reinterpret_cast<void**>(&nativeCameraPoseOriginal))!=MH_OK){removeNativePoseHooks();return false;}
    nativeCameraPoseHooked=true;
    if(MH_EnableHook(address<void>(nativeCameraPoseRva))!=MH_OK||
       MH_EnableHook(address<void>(nativeCameraStateRva))!=MH_OK){removeNativePoseHooks();return false;}
    return true;
}
}

unsigned engine_game_eye(XrSwapchain chain){
    // OpenXR handles are opaque: software providers legitimately issue 1.
    if(!installed.load()||!nativeRuntimeSettings||chain==XR_NULL_HANDLE)return 2;
    // LMU passes these exact two vector elements to xrCreateSwapchain and
    // reads the completed handles back for xrEnumerateSwapchainImages.
    auto* vector=address<std::uintptr_t>(0x19b28d0);
    if(!readable(vector,2*sizeof(std::uintptr_t)))return 2;
    std::uintptr_t begin{},end{};
    std::memcpy(&begin,vector,sizeof(begin));std::memcpy(&end,vector+1,sizeof(end));
    if(!begin||begin%alignof(XrSwapchain)||end<begin||end-begin!=2*sizeof(XrSwapchain)||
       !readable(reinterpret_cast<void*>(begin),2*sizeof(XrSwapchain)))return 2;
    XrSwapchain eyes[2]{};std::memcpy(eyes,reinterpret_cast<void*>(begin),sizeof(eyes));
    const bool left=chain==eyes[0],right=chain==eyes[1];
    // The first eye is enumerated before the second is created. An empty or
    // sentinel sibling does not weaken exact identity of the requested handle.
    return left!=right?(left?0u:1u):2u;
}

bool engine_start(ID3D11DeviceContext* context) {
    if(installed.load()) return true;
    char mode[32]{};GetEnvironmentVariableA("LMU_QV_MODE",mode,sizeof(mode));
    const bool stereoTiming=!std::strcmp(mode,"stereo-timing");
    stereoVrsMode=!std::strcmp(mode,"vrs");
    char prepareSetting[8]{};
    char renderSetting[8]{};
    const bool renderingRequested=!std::strcmp(mode,"quad")&&GetEnvironmentVariableA("LMU_QV_SPS_RENDER",renderSetting,sizeof(renderSetting))==1&&renderSetting[0]=='1';
    spsRenderingOptIn=renderingRequested&&native_sps_device_matches(context);
    if(renderingRequested&&!spsRenderingOptIn)log_line("sps_render unavailable: early same-device shader/query tracking was not established; native rendering retained");
    spsPreparationOptIn=!std::strcmp(mode,"quad")&&(spsRenderingOptIn||
        (GetEnvironmentVariableA("LMU_QV_SPS_PREPARE",prepareSetting,sizeof(prepareSetting))==1&&prepareSetting[0]=='1'));
    char ownerSetting[8]{};
    spsOwnerCaptureOptIn=spsPreparationOptIn&&diagnostics_enabled()&&
        GetEnvironmentVariableA("LMU_QV_SPS_OWNER_INPUTS",ownerSetting,sizeof(ownerSetting))==1&&ownerSetting[0]=='1';
    nativeOwnerInputEnabled.store(false);spsOwnerCapture.reset(spsOwnerCaptureOptIn);
    spsFocusFrame.reset();spsPreparationCounts={};spsPreparedLogged=spsPreparationRefusedLogged=false;
    spsWorldPoseFrame.reset();spsNativePoseProduced=spsNativePoseFallback=0;spsNativePoseLogged=false;
    ++spsNativePoseEpoch;spsWorldPoseEpoch=0;spsNativePoseRejected=false;spsNativePoseSourceReason="native camera-state boundary not observed";
    spsNativeStateCaller=spsNativePoseCaller=0;
    spsExpectedWorldPose={};spsExpectedPoseView=0;spsExpectedPoseIdentity={};spsAwaitingRightPose=false;spsNativeRightPoseChecks=spsNativeRightPoseMismatches=0;spsRightMismatchKinds={};
    spsViewInputFrame.reset();spsNativeViewSettingsVerified=spsNativeViewRejected=spsNativeViewsLogged=spsNativeViewsRefusalLogged=false;
    spsNativeViewProduced=spsNativeViewRefused=0;spsNativeViewMatches[0]=spsNativeViewMatches[1]=0;
    {std::lock_guard<std::mutex> lock(spsOwnerMutex);spsOwnerFrame.reset();spsOwnerFrames=spsOwnerRoots=spsOwnerRefusals=0;
        spsOwnerAdmissionReadyRoots=spsOwnerWorldRefreshOwners=spsOwnerBoundsAdmissionOwners=0;
        spsOwnerReadyLogged=spsOwnerRefusalLogged=false;spsOwnerFrameIdentity={};}
    char auditSetting[8]{};const auto auditLength=GetEnvironmentVariableA("LMU_QV_STEREO_AUDIT",auditSetting,sizeof(auditSetting));
    wchar_t auditDirectory[32768]{};const auto auditDirectoryLength=GetEnvironmentVariableW(L"LMU_QV_STEREO_AUDIT_DIR",auditDirectory,32768);
    bool auditRequested=diagnostics_enabled()&&!stereoTiming&&auditLength==1&&auditSetting[0]=='1'&&auditDirectoryLength&&auditDirectoryLength<32768;
    char spsSetting[8]{},broadProfile[8]{},gpuTriggerSetting[2]{},benchSetting[2]{};
    const bool spsOptIn=GetEnvironmentVariableA("LMU_QV_SPS_PROFILE",spsSetting,sizeof(spsSetting))==1&&spsSetting[0]=='1';
    GetEnvironmentVariableA("LMU_QV_PROFILE",broadProfile,sizeof(broadProfile));
    const bool spsRequested=spsOptIn&&diagnostics_enabled()&&!std::strcmp(mode,"quad")&&
        !(auditLength==1&&auditSetting[0]=='1')&&std::strcmp(broadProfile,"1")&&
        !GetEnvironmentVariableA("LMU_QV_GPU_TIMING_TRIGGER",gpuTriggerSetting,sizeof(gpuTriggerSetting))&&
        !GetEnvironmentVariableA("LMU_QV_BENCH_CONTROL",benchSetting,sizeof(benchSetting));
    if(spsOptIn&&!spsRequested)log_line("sps_cpu_profile disabled: requires quad diagnostics without stereo audit, broad profiling or legacy timing/benchmark");
    char uploadSetting[8]{};nativeUploadOptIn=!stereoTiming&&GetEnvironmentVariableA("LMU_QV_CPU_UPLOAD_REUSE",uploadSetting,sizeof(uploadSetting))==1&&uploadSetting[0]=='1';nativeUploadReuse.reset();
    char outerUploadSetting[8]{};nativeUploadOuterOptIn=!stereoTiming&&GetEnvironmentVariableA("LMU_QV_CPU_OUTER_REUSE",outerUploadSetting,sizeof(outerUploadSetting))==1&&outerUploadSetting[0]=='1';
    if(nativeUploadOuterOptIn)nativeUploadOptIn=true;
    if(nativeUploadOuterOptIn&&auditRequested){auditRequested=false;log_line("cpu_outer_reuse: stereo audit/proof disabled; ordinary render profiling remains available");}
    char batchSetting[8]{};nativeUploadBatchOptIn=!stereoTiming&&GetEnvironmentVariableA("LMU_QV_CPU_UPLOAD_BATCH",batchSetting,sizeof(batchSetting))==1&&batchSetting[0]=='1';
    batchFailureLogged=0;batchFailureMessages=0;
    if(nativeUploadBatchOptIn){nativeUploadOptIn=false;nativeUploadOuterOptIn=false;if(auditRequested){auditRequested=false;log_line("cpu_upload_batch: draw audit/proof disabled for this run; ordinary render profiling remains available");}}
    if(stereoVrsMode){auditRequested=false;nativeUploadOptIn=false;nativeUploadBatchOptIn=false;nativeUploadOuterOptIn=false;}
    if(std::strcmp(mode,"quad") && !stereoTiming && !stereoVrsMode) {log_line("engine: trace mode; no native hooks");return true;}
    base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!correctProcess()) {log_line("engine: unexpected executable name; hooks refused");return false;}
    if(!matches(0xd58a70,render_anchor) ||
       !matches(0x557810,projection_setter_anchor) || !matches(0x557870,derived_anchor) ||
       !matches(0x617090,ui_anchor) || !matches(0x617fb0,ui_eye_anchor) || !matches(0x5de200,scene_anchor) ||
       !matches(0x6188d0,mask_anchor)) {
        log_line("engine: code anchor mismatch; hooks refused");return false;
    }
    char vrsOptIn[8]{};
    const DWORD vrsLength=GetEnvironmentVariableA("LMU_QV_VRS_EXCLUSIVE",vrsOptIn,sizeof(vrsOptIn));
    worldVrsOptIn=!stereoTiming && vrsLength==1 && vrsOptIn[0]=='1';
    char nativeOptIn[8]{};
    const auto nativeLength=GetEnvironmentVariableA("LMU_QV_NATIVE_OUTER",nativeOptIn,sizeof(nativeOptIn));
    nativeOuterOptIn=!stereoVrsMode&&!stereoTiming&&nativeLength==1&&nativeOptIn[0]=='1';nativeOuterFailed=false;
    char ssrOptIn[8]{};
    nativeOuterSsrOptIn=nativeOuterOptIn&&GetEnvironmentVariableA("LMU_QV_OUTER_SSR",ssrOptIn,sizeof(ssrOptIn))==1&&ssrOptIn[0]=='1';
    if(nativeOuterSsrOptIn)for(const auto& anchor:native_outer_ssr_anchors)if(!matches(anchor.rva,anchor.bytes)){
        nativeOuterSsrOptIn=false;
        log_line("engine: SSR code anchor mismatch; original SSR retained with primary outer reduction");
        break;
    }
    if(nativeOuterSsrOptIn)log_line("engine: experimental outer SSR reduction requested; guarded resource bank; focus and PPFX retained");
    nativeQuadSubmissionBlocked=false;
    char runtimeSetting[8]{};GetEnvironmentVariableA("LMU_QV_USE_RUNTIME_SETTINGS",runtimeSetting,sizeof(runtimeSetting));
    nativeRuntimeSettings=!stereoVrsMode&&!stereoTiming&&!std::strcmp(runtimeSetting,"1");nativeOuterSeparateBanks=false;
    fovScaleLogged=false;
    if(nativeRuntimeSettings){
        bool cropAnchors=matches(fovScaleRva,fovScaleAnchor);
        for(auto caller:fovScaleCallers)cropAnchors&=matches(caller-fovScaleCallAnchor.size(),fovScaleCallAnchor);
        if(!cropAnchors){log_line("engine: native FOV crop ABI anchor mismatch; hooks refused");return false;}
    }
    nativeOuterWarmupView=nullptr;nativeOuterWarmupCalls=0;nativeOuterCompleted[0]=nativeOuterCompleted[1]=0;
    nativeOuterPrepareFailures=nativeOuterRetryCalls=0;
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
        char cropWidth[128]{},cropHeight[128]{};
        const auto widthLength=GetEnvironmentVariableA("LMU_QV_CROP_WIDTH_PERCENT",cropWidth,sizeof(cropWidth));
        const auto heightLength=GetEnvironmentVariableA("LMU_QV_CROP_HEIGHT_PERCENT",cropHeight,sizeof(cropHeight));
        nativeCropWidth=parse_crop_percent(widthLength>=sizeof(cropWidth)?"invalid":cropWidth);
        nativeCropHeight=parse_crop_percent(heightLength>=sizeof(cropHeight)?"invalid":cropHeight);
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
    if((worldVrsOptIn||nativeOuterOptIn||auditRequested||spsRequested||nativeUploadOptIn||nativeUploadBatchOptIn) && (!matches(0x51d8a0,vrs_scope::kMainPassAnchor) ||
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
    timingTrigger.clear();timingArmed=false;gpuTiming.reset();timingMask=(stereoTiming||stereoVrsMode)?0x3:0xf;
    pipelineStats.reset();benchmarkPath.clear();benchmarkControl=BenchmarkControl{};benchmarkSnapshot={};
    wchar_t controlPath[32768]{};
    const DWORD controlLength=GetEnvironmentVariableW(L"LMU_QV_BENCH_CONTROL",controlPath,32768);
    if(diagnostics_enabled()&&controlLength&&controlLength<32768&&std::filesystem::path(controlPath).is_absolute()&&!stereoTiming) {
        benchmarkPath=controlPath;
        log_line("engine benchmark: waiting for phaseId rate warmupFrames commands; rates are requests until VRS confirms application");
    }
    wchar_t trigger[32768]{};
    const DWORD length=GetEnvironmentVariableW(L"LMU_QV_GPU_TIMING_TRIGGER",trigger,32768);
    if(diagnostics_enabled()&&benchmarkPath.empty()&&length && length<32768 && std::filesystem::path(trigger).is_absolute()) {
        timingTrigger=trigger;
        log_line("engine GPU timing: waiting for explicit trigger file");
    }
    const std::size_t hookCount=stereoTiming?1:(nativeOuterOptIn?std::size(hooks):((worldVrsOptIn||auditRequested||spsRequested||nativeUploadOptIn||nativeUploadBatchOptIn)?9:(nativeRuntimeSettings?7:5)));
    if(hookCount>6&&!matches(0x8c2d30,nativeEnumerateAnchor)){
        log_line("engine: native XR enumeration anchor mismatch; hooks refused");return false;
    }
    // The FOV-scale hook only changes behavior with runtime settings, and its
    // anchor is only verified in that mode. Never patch it unverified (VRS).
    std::size_t selectedCount=0;
    for(std::size_t i=0;i<hookCount;++i){
        if(i==fovScaleHookIndex&&!nativeRuntimeSettings)continue;
        if(!hookTargetPlausible(hooks[i].rva)){
            char message[160]{};std::snprintf(message,sizeof(message),"engine: hook target %llx is not a function start in this executable; hooks refused",static_cast<unsigned long long>(hooks[i].rva));
            log_line(message);return false;
        }
        selectedHooks[selectedCount++]=i;
    }
    auto status=MH_Initialize();
    if(status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED) {log_line("engine: MH_Initialize failed");return false;}
    std::size_t created=0;
    for(std::size_t i=0;i<selectedCount;++i) {
        const auto& hook=hooks[selectedHooks[i]];
        status=MH_CreateHook(address<void>(hook.rva),hook.detour,hook.original);
        if(status!=MH_OK) {removeHooks(created);log_line("engine: hook creation failed");return false;}
        ++created;
    }
    for(std::size_t i=0;i<selectedCount;++i) {
        const auto& hook=hooks[selectedHooks[i]];
        if(MH_EnableHook(address<void>(hook.rva))!=MH_OK) {
            removeHooks(created);log_line("engine: hook enable failed");return false;
        }
    }
    innerHistory[0]={};innerHistory[1]={};
    installedHookCount=selectedCount;
    if(spsPreparationOptIn){
        if(installNativePoseHooks()){
            spsNativeViewSettingsVerified=nativeViewSettingsTargetsVerified();
            log_line(spsNativeViewSettingsVerified?
                "sps_prepare: native cockpit world-pose and view-setting producers installed; independent eye bounds; native camera updates retained":
                "sps_prepare: native cockpit world-pose producer installed; view-setting anchors unavailable; native preparation retained");
        }
        else log_line("sps_prepare: native cockpit world-pose boundary unavailable; restricted projection preparation retained");
    }
    if(spsOwnerCaptureOptIn&&nativeOwnerTargetVerified()){
        if(MH_CreateHook(address<void>(nativeOwnerRva),reinterpret_cast<void*>(&nativeOwnerHook),reinterpret_cast<void**>(&nativeOwnerOriginal))==MH_OK){
            if(MH_EnableHook(address<void>(nativeOwnerRva))==MH_OK){nativeOwnerInputHooked=true;nativeOwnerInputEnabled.store(true);}
            else MH_RemoveHook(address<void>(nativeOwnerRva));
        }
    }
    if(spsOwnerCaptureOptIn&&!nativeOwnerInputHooked)log_line("sps_prepare owner producer unavailable: native boundary verification/hook failed; native rendering retained");
    if(spsPreparationOptIn&&!spsOwnerCaptureOptIn)log_line("sps_prepare owner diagnostic collection disabled; camera preparation only");
    stereo_audit_configure(context,auditRequested,auditDirectory,log_line);
    const std::array<std::uint8_t,5> prepareCall0{0xe8,0x1c,0x34,0x04,0x00},prepareCall1{0xe8,0x62,0x33,0x04,0x00};
    if((stereo_audit_enabled()||spsRequested||spsRenderingOptIn)&&matches(nativePrepareRva,nativePrepareAnchor)&&hookTargetPlausible(nativePrepareRva)&&matches(0x51f5bf,prepareCall0)&&matches(0x51f679,prepareCall1)){
        if(MH_CreateHook(address<void>(nativePrepareRva),reinterpret_cast<void*>(&nativePrepareHook),reinterpret_cast<void**>(&nativePrepareOriginal))==MH_OK){
            if(MH_EnableHook(address<void>(nativePrepareRva))==MH_OK){nativePrepareHooked=true;log_line(spsRequested?"sps_cpu_profile native preparation timing hook enabled; original calls retained":"stereo_audit native per-mesh preparation hook enabled; pointer identities anonymized; original preparation retained");}
            else MH_RemoveHook(address<void>(nativePrepareRva));
        }
    }
    if (auditRequested && !nativePrepareHooked)
      log_line("stereo_audit native preparation hook unavailable; paired private proof requires "
               "this identity source");
    if((stereo_audit_enabled()||spsRequested||spsRenderingOptIn)&&nativePrepareHooked&&nativeTransformTargetVerified()){
        if(MH_CreateHook(address<void>(nativeTransformRva),reinterpret_cast<void*>(&nativeTransformHook),
                         reinterpret_cast<void**>(&nativeTransformOriginal))==MH_OK){
            if(MH_EnableHook(address<void>(nativeTransformRva))==MH_OK){
                nativeTransformHooked=true;
                log_line(spsRequested?"sps_cpu_profile native transform timing hook enabled; no numeric snapshots":"stereo_audit original transform helper hook enabled; bounded entry/exit comparison; native preparation/draws retained");
            }else MH_RemoveHook(address<void>(nativeTransformRva));
        }
    }
    stereo_audit_native_transform_hook_available(nativeTransformHooked);
    if(auditRequested&&!nativeTransformHooked)log_line("stereo_audit native transform helper hook unavailable");
    if (stereo_audit_enabled() && nativeClassifyTargetVerified()) {
      if (MH_CreateHook(address<void>(nativeClassifyRva),
                        reinterpret_cast<void *>(&nativeClassifyHook),
                        reinterpret_cast<void **>(&nativeClassifyOriginal)) == MH_OK) {
        if (MH_EnableHook(address<void>(nativeClassifyRva)) == MH_OK) {
          nativeClassifyHooked = true;
          log_line("stereo_audit original classifier diagnostic hook enabled; copied numeric "
                   "export only; native invocation retained");
        } else
          MH_RemoveHook(address<void>(nativeClassifyRva));
      }
    }
    stereo_audit_native_classify_hook_available(nativeClassifyHooked);
    if (auditRequested && !nativeClassifyHooked)
      log_line("stereo_audit native classifier diagnostic hook unavailable");
    const std::array<std::uint8_t,5> uploadCall{0xe8,0x1c,0x4c,0x01,0x00};
    const std::array<std::uint8_t,15> uploadBindAnchor{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x20};
    const std::array<std::uint8_t,6> lightUploadSize{0x41,0xb8,0x80,0x0a,0x00,0x00};
    if(nativeUploadBatchOptIn&&(!matches(0x5cae5c,lightUploadSize)||!nativeUploadBatch.configure(context))){nativeUploadBatchOptIn=false;log_line("cpu_upload_batch unavailable: native layout or D3D11.1 constant-buffer offsets unsupported; native uploads retained");}
    if((nativeUploadOptIn||nativeUploadBatchOptIn||spsRequested)&&matches(nativeUploadRva,nativeUploadAnchor)&&hookTargetPlausible(nativeUploadRva)&&matches(0x5b602f,uploadCall)&&matches(0x5c3f20,uploadBindAnchor)){
        if(MH_CreateHook(address<void>(nativeUploadRva),reinterpret_cast<void*>(&nativeUploadHook),reinterpret_cast<void**>(&nativeUploadOriginal))==MH_OK){
            if(MH_EnableHook(address<void>(nativeUploadRva))==MH_OK){nativeUploadHooked=true;
                log_line(spsRequested&&!nativeUploadOptIn&&!nativeUploadBatchOptIn?"sps_cpu_profile native upload timing hook enabled; uploads unchanged":nativeUploadBatchOptIn?"cpu_upload_batch enabled: outer/focus opaque group 31; unchanged-upload filtering + adaptive small-CB batching; large CBs stay native; native shaders/draws retained; experimental":
                    nativeUploadOuterOptIn?"cpu_upload_reuse enabled: outer/focus opaque group 31; reuse within each group only; native preparation/draws/bindings retained; experimental":
                    "cpu_upload_reuse enabled: focus opaque group 31; byte-identical native VS constants only; all draws/bindings retained; experimental");}
            else MH_RemoveHook(address<void>(nativeUploadRva));
        }
    }
    if(nativeUploadOptIn&&!nativeUploadHooked){nativeUploadOptIn=false;nativeUploadOuterOptIn=false;log_line("cpu_upload_reuse unavailable: native code anchor/hook mismatch; ordinary rendering retained");}
    if(nativeUploadBatchOptIn&&!nativeUploadHooked){nativeUploadBatchOptIn=false;nativeUploadBatch.reset();log_line("cpu_upload_batch unavailable: native code anchor/hook mismatch; native uploads retained");}
    if(spsRequested&&(!nativePrepareHooked||!nativeTransformHooked||!nativeUploadHooked||
       !sps_cpu_hooks::configure(context,log_line)))
        log_line("sps_cpu_profile unavailable: required native/D3D11 timing hook missing; no complete CPU attribution possible");
    char profileSetting[8]{};GetEnvironmentVariableA("LMU_QV_PROFILE",profileSetting,sizeof(profileSetting));
    const bool profiling=diagnostics_enabled()&&!stereoTiming&&!std::strcmp(profileSetting,"1")&&benchmarkPath.empty()&&timingTrigger.empty();
    char durationSetting[16]{};char* durationEnd=nullptr;
    const auto durationLength=GetEnvironmentVariableA("LMU_QV_PROFILE_SECONDS",durationSetting,sizeof(durationSetting));
    const auto requestedSeconds=durationLength&&durationLength<sizeof(durationSetting)?std::strtoul(durationSetting,&durationEnd,10):0;
    const bool validDuration=durationLength&&durationLength<sizeof(durationSetting)&&durationEnd==durationSetting+durationLength&&requestedSeconds>=30&&requestedSeconds<=300;
    const auto durationSeconds=validDuration?unsigned(requestedSeconds):120u;
    if(profiling&&durationLength&&!validDuration)log_line("render_profile invalid duration; using 120 seconds (allowed 30..300)");
    renderProfile.configure(profiling,log_line,durationSeconds,flush_log);
    if(!std::strcmp(profileSetting,"1")&&!profiling)log_line("render_profile disabled: incompatible legacy timing/benchmark or stereo mode");
    installed.store(true);
    if(spsRenderingOptIn&&(!nativePrepareHooked||!nativeTransformHooked||!nativeCameraStateHooked||!nativeCameraPoseHooked||!spsNativeViewSettingsVerified)){
        spsRenderingOptIn=false;native_sps_stop();log_line("sps_render unavailable: complete native preparation/camera hooks required; native rendering retained");
    }
    if(spsRenderingOptIn)log_line("sps_render enabled: bounded paired focus draw replacement with actual right-input verification and native fallback");
    if(spsPreparationOptIn)log_line("sps_prepare enabled: early owned focus camera pair; existing native rendering order and fallback; no profiling required");
    log_line(stereoTiming ? "engine: verified build; stereo timing render hook only; no quad repeats or projection/UI changes" :
                           "engine: verified build; render/UI/projection/mask hooks enabled");
    return true;
}
void engine_stop() {
    spsRenderingOptIn=false;spsRenderFrameIdentity={};native_sps_stop();
    ++spsNativePoseEpoch;
    nativeOwnerInputEnabled.store(false);
    if(nativeOwnerInputHooked){MH_DisableHook(address<void>(nativeOwnerRva));MH_RemoveHook(address<void>(nativeOwnerRva));nativeOwnerInputHooked=false;}
    {std::lock_guard<std::mutex> lock(spsOwnerMutex);
        if(spsOwnerCaptureOptIn){char line[512]{};std::snprintf(line,sizeof(line),
            "sps_prepare owner summary complete_frames=%llu copied_roots=%llu refused_frames=%llu admission_ready_roots=%llu world_refresh_owners=%llu bounds_admission_owners=%llu; counts are repeated frame observations; native effects/GPU resources not replaced",
            static_cast<unsigned long long>(spsOwnerFrames),static_cast<unsigned long long>(spsOwnerRoots),static_cast<unsigned long long>(spsOwnerRefusals),
            static_cast<unsigned long long>(spsOwnerAdmissionReadyRoots),static_cast<unsigned long long>(spsOwnerWorldRefreshOwners),static_cast<unsigned long long>(spsOwnerBoundsAdmissionOwners));log_line(line);}
        spsOwnerFrame.reset();spsOwnerFrameIdentity={};spsOwnerCapture.reset();}
    if(spsPreparationOptIn){char line[384]{};std::snprintf(line,sizeof(line),
        "sps_prepare summary prepared_pairs=%llu refused_pairs=%llu prepared_projection_left=%llu prepared_projection_right=%llu fallback_left=%llu fallback_right=%llu; native eye draws retained",
        static_cast<unsigned long long>(spsPreparationCounts.prepared),static_cast<unsigned long long>(spsPreparationCounts.refused),
        static_cast<unsigned long long>(spsPreparationCounts.used[0]),static_cast<unsigned long long>(spsPreparationCounts.used[1]),
        static_cast<unsigned long long>(spsPreparationCounts.fallback[0]),static_cast<unsigned long long>(spsPreparationCounts.fallback[1]));log_line(line);}
    spsPreparationOptIn=false;spsFocusFrame.reset();
    {char line[320]{};std::snprintf(line,sizeof(line),
        "sps_prepare native view summary produced=%llu refused=%llu left_matches=%llu right_matches=%llu; native traversal/draws retained",
        static_cast<unsigned long long>(spsNativeViewProduced),static_cast<unsigned long long>(spsNativeViewRefused),
        static_cast<unsigned long long>(spsNativeViewMatches[0]),static_cast<unsigned long long>(spsNativeViewMatches[1]));
        if(spsNativeViewSettingsVerified)log_line(line);}
    spsViewInputFrame.reset();spsNativeViewSettingsVerified=false;spsNativeViewRejected=false;
    if(nativeCameraStateHooked||nativeCameraPoseHooked){char line[512]{};std::snprintf(line,sizeof(line),
        "sps_prepare native world pose summary produced=%llu fallback=%llu right_checks=%llu right_mismatches=%llu mismatch_mode=%llu mismatch_position=%llu mismatch_basis=%llu mismatch_translation=%llu; all right comparisons counted; detailed_samples_max=8",
        static_cast<unsigned long long>(spsNativePoseProduced),static_cast<unsigned long long>(spsNativePoseFallback),
        static_cast<unsigned long long>(spsNativeRightPoseChecks),static_cast<unsigned long long>(spsNativeRightPoseMismatches),
        static_cast<unsigned long long>(spsRightMismatchKinds[0]),static_cast<unsigned long long>(spsRightMismatchKinds[1]),
        static_cast<unsigned long long>(spsRightMismatchKinds[2]),static_cast<unsigned long long>(spsRightMismatchKinds[3]));log_line(line);}
    removeNativePoseHooks();
    sps_cpu_hooks::stop();
    stereo_audit_stop();
    if(nativeTransformHooked){
        MH_DisableHook(address<void>(nativeTransformRva));MH_RemoveHook(address<void>(nativeTransformRva));
        nativeTransformHooked=false;
    }
    stereo_audit_native_transform_hook_available(false);
    if (nativeClassifyHooked) {
      MH_DisableHook(address<void>(nativeClassifyRva));
      MH_RemoveHook(address<void>(nativeClassifyRva));
      nativeClassifyHooked = false;
    }
    stereo_audit_native_classify_hook_available(false);
    if(nativePrepareHooked){MH_DisableHook(address<void>(nativePrepareRva));MH_RemoveHook(address<void>(nativePrepareRva));nativePrepareHooked=false;}
    {std::lock_guard<std::recursive_mutex> uploadLock(nativeUploadMutex);
    const bool uploadWasEnabled=nativeUploadOptIn.exchange(false),batchWasEnabled=nativeUploadBatchOptIn.exchange(false);
    nativeUploadOuterOptIn=false;
    nativeUploadBatch.end(); // An active scope restores on its owner before trampoline removal.
    if(nativeUploadHooked){MH_DisableHook(address<void>(nativeUploadRva));MH_RemoveHook(address<void>(nativeUploadRva));nativeUploadHooked=false;}
    if(uploadWasEnabled&&diagnostics_enabled()){const auto& c=nativeUploadReuse.counters();char line[448]{};std::snprintf(line,sizeof(line),"cpu_upload_reuse summary examined=%llu skipped=%llu bytes_skipped=%llu learned=%llu aliases=%llu invalidated=%llu topology_checks=%llu; native preparation retained; counters are not a frametime benchmark",static_cast<unsigned long long>(c.examined),static_cast<unsigned long long>(c.skipped),static_cast<unsigned long long>(c.bytesSkipped),static_cast<unsigned long long>(c.learned),static_cast<unsigned long long>(c.aliased),static_cast<unsigned long long>(c.invalidated),static_cast<unsigned long long>(c.topologyChecks));log_line(line);}
    if(batchWasEnabled)logUploadBatchStats("summary");
    selectedUploadCache=nullptr;selectedUploadContext=nullptr;nativeUploadReuse.reset();nativeUploadBatch.reset();}
    nativeOuterOptIn=false;nativeOuterSsrOptIn=false;
    for(auto& bank:nativeOuterBanks)if(!bank.reset())log_line("native_outer teardown on different thread; native bank retained until process exit");
    if(!installed.exchange(false)) {
        renderProfile.shutdown();
        resetWorldVrs();worldVrsOptIn=false;worldVrsDefaultRate=1;worldVrsCounters={};
        gpuTiming.reset();pipelineStats.reset();benchmarkPath.clear();benchmarkControl=BenchmarkControl{};
        benchmarkSnapshot={};timingTrigger.clear();timingArmed=false;return;
    }
    removeHooks(installedHookCount);installedHookCount=0;
    renderProfile.shutdown();
    logVrsCounters();resetWorldVrs();worldVrsOptIn=false;worldVrsDefaultRate=1;worldVrsCounters={};
    gpuTiming.reset();pipelineStats.reset();benchmarkPath.clear();benchmarkControl=BenchmarkControl{};
    benchmarkSnapshot={};timingTrigger.clear();timingArmed=false;
    log_line("engine: hooks removed");
}
} // namespace lmuqv
