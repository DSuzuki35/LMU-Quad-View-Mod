#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "xr_layer.hpp"
#include "xr_capture.hpp"
#include "focus_overlay.hpp"
#include "xr_sizes.hpp"
#include "focus_box.hpp"
#include "outer_resample.hpp"
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr char layerName[] = "XR_APILAYER_LMU_quadview";
std::recursive_mutex stateMutex;
std::mutex logMutex;
template<class T> T structure(XrStructureType type) { T value{}; value.type=type; return value; }
void logf(const char* format, ...) {
    char text[2048]{};
    va_list args; va_start(args,format); std::vsnprintf(text,sizeof(text),format,args); va_end(args);
    lmuqv::log_line(text);
}
bool quadMode() {
    char value[32]{};
    GetEnvironmentVariableA("LMU_QV_MODE",value,sizeof(value));
    return !std::strcmp(value,"quad");
}
std::wstring environmentWide(const wchar_t* name) {
    DWORD size=GetEnvironmentVariableW(name,nullptr,0);if(!size||size>32768)return {};
    std::wstring value(size,L'\0');DWORD written=GetEnvironmentVariableW(name,value.data(),size);
    if(written>=size)return {};
    value.resize(written);return value;
}
bool gameProcess() {
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768);
    return !_wcsicmp(std::filesystem::path(path).filename().c_str(),L"Le Mans Ultimate.exe");
}
struct Dispatch {
    PFN_xrGetInstanceProcAddr get{};
#define XR_NEXT_MEMBERS(X) \
    X(DestroyInstance) X(EnumerateViewConfigurations) X(GetViewConfigurationProperties) \
    X(EnumerateViewConfigurationViews) X(EnumerateEnvironmentBlendModes) X(CreateSession) \
    X(DestroySession) X(BeginSession) X(EndSession) X(WaitFrame) X(BeginFrame) X(EndFrame) \
    X(LocateViews) X(CreateSwapchain) X(DestroySwapchain) X(EnumerateSwapchainImages) \
    X(AcquireSwapchainImage) X(WaitSwapchainImage) X(ReleaseSwapchainImage)
#define DECLARE(name) PFN_xr##name name{};
    XR_NEXT_MEMBERS(DECLARE)
#undef DECLARE
    bool load(XrInstance instance) {
        bool okay=true;
#define LOAD(name) { PFN_xrVoidFunction fn{}; XrResult r=get(instance,"xr" #name,&fn); name=reinterpret_cast<PFN_xr##name>(fn); if(XR_FAILED(r)||!name) {logf("Missing downstream xr%s: %d",#name,r);okay=false;} }
        XR_NEXT_MEMBERS(LOAD)
#undef LOAD
        return okay;
    }
} next;
lmuqv::OuterResample outerResample; // Owns COM state; deliberately outside the copy-resettable XR State.
struct Outer {
    XrSwapchain chain{};
    std::vector<XrSwapchainImageD3D11KHR> images;
    std::vector<ID3D11RenderTargetView*> rtvs;
    bool acquired=false, waited=false;
    uint32_t index=0;
    uint32_t width=0,height=0;
};
struct AppImageOwnership { uint32_t index=0;bool waited=false; };
struct AppChain {
    XrSwapchainCreateInfo info{};
    unsigned eye=2;
    // Borrowed runtime textures; never dereference after successful DestroySwapchain.
    std::vector<ID3D11Texture2D*> images;
    std::vector<ID3D11RenderTargetView*> focusRtvs;
    std::deque<AppImageOwnership> acquired;
    bool trackingInvalid=false;
};
struct State {
    XrInstance instance{};
    XrSession session{};
    XrSystemId system{};
    bool quad=false, running=false, frame=false, shouldRender=false, located=false;
    bool captures[2]{};
    bool sceneFocus[2]{};
    bool debugFocus=false;
    bool debugDrawn[4]{};
    bool waitPending=false, waitShouldRender=false;
    XrTime pendingTime{};
    XrTime predictedTime{}, locateTime{};
    XrSpace locateSpace{};
    XrView views[4]{};
    ID3D11Device* device{};
    ID3D11DeviceContext* context{};
    Outer outer[2];
    XrSwapchainCreateInfo colorInfo{};
    std::unordered_map<XrSwapchain,AppChain> appChains;
    unsigned nextAppEye=0;
    bool runtimeSettings=false,runtimeSizesReady=false,rawFovLogged=false;
    uint32_t runtimeFocusWidth=0,runtimeFocusHeight=0;
    uint32_t runtimeOuterWidth[2]{},runtimeOuterHeight[2]{};
    lmuqv::XrOutputSizes outputSizes;
    lmuqv::FocusBoxSize focusBox;
    bool pixelEnabled=false,pixelDone=false,pixelRequireScene=false;
    uint64_t pixelFrame=0;
    unsigned pixelMask=0,pixelSuccessMask=0;
    lmuqv::capture_detail::Config capture;
    unsigned captureMask=0;
    uint64_t frames=0;
} s;
void clearFrame() {
    s.frame=false;s.shouldRender=false;s.located=false;s.captures[0]=s.captures[1]=false;
    s.sceneFocus[0]=s.sceneFocus[1]=false;
    std::fill(std::begin(s.debugDrawn),std::end(s.debugDrawn),false);
}
void clearFocusRtvs(AppChain& app){for(auto* rtv:app.focusRtvs)if(rtv)rtv->Release();app.focusRtvs.clear();}
bool sameFocusPose(unsigned eye){
    const auto& a=s.views[eye].pose;const auto& b=s.views[eye+2].pose;
    float sign=a.orientation.x*b.orientation.x+a.orientation.y*b.orientation.y+a.orientation.z*b.orientation.z+a.orientation.w*b.orientation.w<0?-1.f:1.f;
    return std::abs(a.position.x-b.position.x)<1e-5f&&std::abs(a.position.y-b.position.y)<1e-5f&&std::abs(a.position.z-b.position.z)<1e-5f&&
           std::abs(a.orientation.x-sign*b.orientation.x)<1e-5f&&std::abs(a.orientation.y-sign*b.orientation.y)<1e-5f&&
           std::abs(a.orientation.z-sign*b.orientation.z)<1e-5f&&std::abs(a.orientation.w-sign*b.orientation.w)<1e-5f;
}
void drawFocusDebug(unsigned eye,bool inner,ID3D11RenderTargetView* target){
    unsigned slot=eye+(inner?2:0);
    if(eye>1||!target||!s.debugFocus||!s.quad||!s.frame||!s.shouldRender||!s.located||!s.captures[eye]||s.locateTime!=s.predictedTime||s.debugDrawn[slot]||!sameFocusPose(eye))return;
    auto angles=[](XrFovf f){return lmuqv::FocusFov{f.angleLeft,f.angleRight,f.angleUp,f.angleDown};};
    bool drawn=lmuqv::draw_focus_border(s.context,target,angles(s.views[eye+(inner?2:0)].fov),angles(s.views[eye+2].fov),true);
    if(drawn)s.debugDrawn[slot]=true;
    if(s.frames<10||s.frames%120==0)logf("debug_focus frame=%llu eye=%u target=%s drawn=%d (runtime focus FOV marker)",static_cast<unsigned long long>(s.frames),eye,inner?"inner":"outer",drawn);
}
void drawAppFocusDebug(AppChain& app){
    if(!s.debugFocus||app.eye>1||app.acquired.empty()||!app.acquired.front().waited||app.acquired.front().index>=app.images.size())return;
    if(app.info.arraySize!=1||app.info.sampleCount!=1||app.info.mipCount!=1)return;
    if(app.focusRtvs.size()!=app.images.size()){clearFocusRtvs(app);app.focusRtvs.resize(app.images.size(),nullptr);}
    unsigned index=app.acquired.front().index;
    if(!app.focusRtvs[index]){
        HRESULT hr=lmuqv::focus_detail::create_rtv(s.device,app.images[index],static_cast<DXGI_FORMAT>(app.info.format),&app.focusRtvs[index]);
        if(FAILED(hr)){if(s.frames<10||s.frames%120==0)logf("debug_focus inner RTV failed eye=%u result=%08lx",app.eye,static_cast<unsigned long>(hr));return;}
    }
    drawFocusDebug(app.eye,true,app.focusRtvs[index]);
}
bool readPixelStats(ID3D11Texture2D* texture,const char* slot,unsigned slotIndex) {
    if(!texture||!s.device||!s.context)return false;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    bool bgra=desc.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    bool rgba=desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if((!rgba&&!bgra)||desc.SampleDesc.Count!=1||desc.ArraySize!=1||desc.MipLevels!=1||!desc.Width||!desc.Height){
        logf("pixel_stats_error slot=%s unsupported texture=%ux%u format=%u samples=%u array=%u mips=%u",slot,desc.Width,desc.Height,desc.Format,desc.SampleDesc.Count,desc.ArraySize,desc.MipLevels);return false;
    }
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ID3D11Texture2D* staging=nullptr;HRESULT hr=s.device->CreateTexture2D(&desc,nullptr,&staging);
    if(FAILED(hr)){logf("pixel_stats_error slot=%s createStaging=%08lx",slot,static_cast<unsigned long>(hr));return false;}
    s.context->CopyResource(staging,texture);
    D3D11_MAPPED_SUBRESOURCE map{};hr=s.context->Map(staging,0,D3D11_MAP_READ,0,&map);
    if(FAILED(hr)){logf("pixel_stats_error slot=%s map=%08lx",slot,static_cast<unsigned long>(hr));staging->Release();return false;}
    if(!map.pData||map.RowPitch<uint64_t(desc.Width)*4){s.context->Unmap(staging,0);staging->Release();return false;}
    uint64_t hash=14695981039346656037ull,nonzero=0;
    unsigned first[4]{};
    const auto* data=static_cast<const unsigned char*>(map.pData);
    for(unsigned component=0;component<4;++component)first[component]=data[bgra&&component<3?2-component:component];
    for(uint32_t y=0;y<desc.Height;++y){
        const auto* row=data+size_t(y)*map.RowPitch;
        for(uint32_t x=0;x<desc.Width;++x){
            const auto* pixel=row+size_t(x)*4;nonzero+=(pixel[0]|pixel[1]|pixel[2])!=0;
            for(unsigned component=0;component<4;++component){hash^=pixel[component];hash*=1099511628211ull;}
        }
    }
    if(s.capture.requested){
        DWORD error=0;
        if(lmuqv::capture_detail::write_bmp(s.capture,slotIndex,desc.Width,desc.Height,map.RowPitch,data,bgra,error)){
            s.captureMask|=1u<<slotIndex;
            auto path=s.capture.directory/(std::string(slot)+".bmp");
            logf("capture_bmp frame=%llu slot=%s path=%s bytes=%llu (offscreen texture before release)",static_cast<unsigned long long>(s.pixelFrame),slot,path.u8string().c_str(),static_cast<unsigned long long>(122+uint64_t(desc.Width)*desc.Height*4));
        }else logf("capture_bmp_error frame=%llu slot=%s error=%lu (no overwrite; partial file retained if write failed)",static_cast<unsigned long long>(s.pixelFrame),slot,error);
    }
    s.context->Unmap(staging,0);staging->Release();
    logf("pixel_stats frame=%llu slot=%s width=%u height=%u nonzeroRGB=%llu pixels=%llu hash=%016llx firstRGBA=%u,%u,%u,%u resourceFormat=%u",static_cast<unsigned long long>(s.pixelFrame),slot,desc.Width,desc.Height,static_cast<unsigned long long>(nonzero),static_cast<unsigned long long>(uint64_t(desc.Width)*desc.Height),static_cast<unsigned long long>(hash),first[0],first[1],first[2],first[3],desc.Format);
    return true;
}
void diagnosePixelSlot(unsigned slot,ID3D11Texture2D* texture) {
    if(!s.pixelEnabled||s.pixelDone||s.pixelFrame!=s.frames||!s.frame||!s.shouldRender||slot>=4||(s.pixelMask&(1u<<slot))||
       (s.pixelRequireScene&&(!s.sceneFocus[0]||!s.sceneFocus[1])))return;
    static const char* names[]={"outer0","outer1","app0","app1"};
    s.pixelMask|=1u<<slot;
    if(readPixelStats(texture,names[slot],slot))s.pixelSuccessMask|=1u<<slot;
}
bool releaseOuters() {
    bool okay=true;
    for(unsigned eye=0;eye<2;++eye) {
        auto& outer=s.outer[eye];
        if(!outer.acquired)continue;
        if(!outer.waited){
            auto wait=structure<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);wait.timeout=0;
            XrResult r=next.WaitSwapchainImage(outer.chain,&wait);
            if(r!=XR_SUCCESS){logf("outer cleanup wait eye=%u result=%d (acquisition retained)",eye,r);okay=false;continue;}
            outer.waited=true;
        }
        if(outer.index<outer.rtvs.size())drawFocusDebug(eye,false,outer.rtvs[outer.index]);
        if(outer.index<outer.images.size())diagnosePixelSlot(eye,outer.images[outer.index].texture);
        auto release=structure<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
        XrResult r=next.ReleaseSwapchainImage(outer.chain,&release);
        if(XR_SUCCEEDED(r)){outer.acquired=false;outer.waited=false;}
        else {logf("release outer %u result=%d",eye,r);okay=false;}
    }
    return okay;
}
void destroyOuter() {
    releaseOuters();
    for(auto& eye:s.outer) {
        for(auto* rtv:eye.rtvs) if(rtv)rtv->Release();
        if(eye.chain) { XrResult r=next.DestroySwapchain(eye.chain);logf("destroy outer swapchain result=%d acquired=%d",r,eye.acquired); }
        eye=Outer{};
    }
    s.colorInfo={};
}
void clearSession() {
    outerResample.reset();
    for(auto& entry:s.appChains)clearFocusRtvs(entry.second);
    destroyOuter();
    if(s.context) s.context->Release();
    if(s.device) s.device->Release();
    s.context=nullptr;s.device=nullptr;s.session=XR_NULL_HANDLE;s.running=false;
    s.appChains.clear();s.nextAppEye=0;s.waitPending=false;clearFrame();
}
XrResult queryQuad(XrInstance instance,XrSystemId system,std::array<XrViewConfigurationView,4>& out) {
    std::array<XrFoveatedViewConfigurationViewVARJO,4> foveated{};
    for(unsigned i=0;i<4;++i) {
        out[i]=structure<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW);
        foveated[i]=structure<XrFoveatedViewConfigurationViewVARJO>(XR_TYPE_FOVEATED_VIEW_CONFIGURATION_VIEW_VARJO);
        foveated[i].foveatedRenderingActive=XR_TRUE;out[i].next=&foveated[i];
    }
    uint32_t count=0;
    XrResult r=next.EnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO,4,&count,out.data());
    for(auto& v:out) v.next=nullptr;
    if(XR_FAILED(r)) return r;
    return count==4 ? XR_SUCCESS : XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
}
XrResult selectRuntimeSizes(const std::array<XrViewConfigurationView,4>& runtime){
    const uint32_t width=(std::max)(runtime[2].recommendedImageRectWidth,runtime[3].recommendedImageRectWidth);
    const uint32_t height=(std::max)(runtime[2].recommendedImageRectHeight,runtime[3].recommendedImageRectHeight);
    logf("runtime recommendations outer0=%ux%u outer1=%ux%u focus0=%ux%u focus1=%ux%u",runtime[0].recommendedImageRectWidth,runtime[0].recommendedImageRectHeight,runtime[1].recommendedImageRectWidth,runtime[1].recommendedImageRectHeight,runtime[2].recommendedImageRectWidth,runtime[2].recommendedImageRectHeight,runtime[3].recommendedImageRectWidth,runtime[3].recommendedImageRectHeight);
    bool valid=width&&height&&width<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION&&height<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    for(unsigned eye=0;eye<2;++eye){
        const auto& outer=runtime[eye];const auto& focus=runtime[eye+2];
        valid=valid&&outer.recommendedImageRectWidth&&outer.recommendedImageRectHeight&&
            outer.recommendedImageRectWidth<=width&&outer.recommendedImageRectHeight<=height&&
            outer.recommendedImageRectWidth<=outer.maxImageRectWidth&&outer.recommendedImageRectHeight<=outer.maxImageRectHeight&&
            width<=focus.maxImageRectWidth&&height<=focus.maxImageRectHeight;
    }
    if(!valid){logf("runtime sizing incompatible: outer recommendations must be nonzero and <= common focus %ux%u on EACH axis and within runtime/device limits; refusing session/hooks",width,height);return XR_ERROR_LIMIT_REACHED;}
    if(s.session&&s.runtimeSizesReady){
        bool same=width==s.runtimeFocusWidth&&height==s.runtimeFocusHeight;
        for(unsigned eye=0;eye<2;++eye)same=same&&runtime[eye].recommendedImageRectWidth==s.runtimeOuterWidth[eye]&&runtime[eye].recommendedImageRectHeight==s.runtimeOuterHeight[eye];
        if(!same){logf("Runtime recommendations changed during session; restart required");return XR_ERROR_RUNTIME_FAILURE;}
    }
    s.runtimeFocusWidth=width;s.runtimeFocusHeight=height;
    for(unsigned eye=0;eye<2;++eye){s.runtimeOuterWidth[eye]=runtime[eye].recommendedImageRectWidth;s.runtimeOuterHeight[eye]=runtime[eye].recommendedImageRectHeight;}
    s.runtimeSizesReady=true;
    logf("Runtime selected exact outer0=%ux%u outer1=%ux%u common focus=%ux%u; no percentage scaling",s.runtimeOuterWidth[0],s.runtimeOuterHeight[0],s.runtimeOuterWidth[1],s.runtimeOuterHeight[1],width,height);
    return XR_SUCCESS;
}
bool createOuter(const XrSwapchainCreateInfo& info) {
    if(s.outer[0].chain && s.outer[1].chain) return true;
    if(!s.device || info.sampleCount!=1 || info.arraySize!=1 || info.faceCount!=1 || info.mipCount!=1 ||
       info.createFlags || !(info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)) {
        logf("Unsupported app color swapchain for outer images: %ux%u samples=%u array=%u mips=%u flags=%llu",info.width,info.height,info.sampleCount,info.arraySize,info.mipCount,static_cast<unsigned long long>(info.createFlags));
        return false;
    }
    XrSwapchainCreateInfo own=info;own.next=nullptr;
    own.width=lmuqv::outer_dimension(info.width,s.outputSizes);own.height=lmuqv::outer_dimension(info.height,s.outputSizes);
    if(!own.width||!own.height)return false;
    own.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    for(unsigned i=0;i<2;++i) {
        auto& eye=s.outer[i];
        if(s.runtimeSettings){own.width=s.runtimeOuterWidth[i];own.height=s.runtimeOuterHeight[i];}
        XrResult r=next.CreateSwapchain(s.session,&own,&eye.chain);
        if(XR_FAILED(r)) { logf("create outer %u result=%d",i,r);destroyOuter();return false; }
        eye.width=own.width;eye.height=own.height;
        uint32_t count=0;r=next.EnumerateSwapchainImages(eye.chain,0,&count,nullptr);
        if(XR_FAILED(r)||!count||count>64) {logf("outer image count %u result=%d count=%u",i,r,count);destroyOuter();return false;}
        eye.images.resize(count);
        for(auto& image:eye.images) image=structure<XrSwapchainImageD3D11KHR>(XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR);
        r=next.EnumerateSwapchainImages(eye.chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data()));
        if(XR_FAILED(r)||count>eye.images.size()) {logf("enumerate outer %u result=%d",i,r);destroyOuter();return false;}
        eye.images.resize(count);
        eye.rtvs.resize(count,nullptr);
        D3D11_RENDER_TARGET_VIEW_DESC desc{};
        const auto swapchainFormat=static_cast<DXGI_FORMAT>(info.format);
        // LMU's HUD is drawn through an UNORM RTV before its SRGB swapchain copy.
        // Prefer the same linear RTV on typeless runtime images to avoid encoding UI twice.
        auto uiFormat=swapchainFormat;
        if(uiFormat==DXGI_FORMAT_R8G8B8A8_TYPELESS||uiFormat==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)uiFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
        if(uiFormat==DXGI_FORMAT_B8G8R8A8_TYPELESS||uiFormat==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)uiFormat=DXGI_FORMAT_B8G8R8A8_UNORM;
        if(uiFormat==DXGI_FORMAT_B8G8R8X8_TYPELESS||uiFormat==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)uiFormat=DXGI_FORMAT_B8G8R8X8_UNORM;
        desc.Format=uiFormat;desc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        for(uint32_t image=0;image<count;++image){
            HRESULT hr=eye.images[image].texture?s.device->CreateRenderTargetView(eye.images[image].texture,&desc,&eye.rtvs[image]):E_POINTER;
            if(FAILED(hr)&&eye.images[image].texture&&uiFormat!=swapchainFormat){
                logf("outer linear RTV unsupported eye=%u image=%u format=%u result=%08lx; fallback swapchain RTV format=%u (UI gamma may differ)",i,image,uiFormat,static_cast<unsigned long>(hr),swapchainFormat);
                desc.Format=swapchainFormat;
                hr=s.device->CreateRenderTargetView(eye.images[image].texture,&desc,&eye.rtvs[image]);
            }
            if(FAILED(hr)){logf("outer RTV eye=%u image=%u result=%08lx",i,image,static_cast<unsigned long>(hr));destroyOuter();return false;}
            D3D11_TEXTURE2D_DESC resource{};eye.images[image].texture->GetDesc(&resource);
            logf("outer RTV eye=%u image=%u resourceFormat=%u swapchainFormat=%u uiFormat=%u",i,image,resource.Format,swapchainFormat,desc.Format);
            desc.Format=uiFormat;
        }
    }
    s.colorInfo=info;s.colorInfo.next=nullptr; // App/source validation uses focus dimensions, not outer output dimensions.
    if(!s.runtimeSettings)logf("Created two outer swapchains format=%lld outer=%ux%u appFocus=%ux%u outerPixelsPercent=%g focusPixelsPercent=%g",static_cast<long long>(info.format),own.width,own.height,info.width,info.height,s.outputSizes.outerPercent,s.outputSizes.focusPercent);
    if(s.runtimeSettings)logf("Actual swapchains outer0=%ux%u outer1=%ux%u focus=%ux%u",s.outer[0].width,s.outer[0].height,s.outer[1].width,s.outer[1].height,info.width,info.height);
    return true;
}
// CopyResource permits different views in the same DXGI typeless family.
unsigned formatFamily(DXGI_FORMAT f) {
    unsigned v=unsigned(f);
    if(v>=27&&v<=32) return 27; // R8G8B8A8
    if(v==87||v==90||v==91) return 87; // B8G8R8A8
    if(v==88||v==92||v==93) return 88; // B8G8R8X8
    if(v>=9&&v<=14) return 9; // R16G16B16A16
    if(v>=23&&v<=25) return 23;
    if(v>=1&&v<=4) return 1;
    return v;
}

XrResult XRAPI_CALL layerDestroyInstance(XrInstance instance) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    logf("xrDestroyInstance");
    if(s.session) {lmuqv::engine_stop();clearSession();}
    XrResult r=next.DestroyInstance(instance);
    if(XR_SUCCEEDED(r)) s=State{};
    return r;
}
XrResult XRAPI_CALL layerEnumerateViewConfigurations(XrInstance instance,XrSystemId system,uint32_t cap,uint32_t* count,XrViewConfigurationType* out) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    logf("xrEnumerateViewConfigurations cap=%u quad=%d",cap,s.quad);
    if(!s.quad) return next.EnumerateViewConfigurations(instance,system,cap,count,out);
    if(!count || (cap&&!out)) return XR_ERROR_VALIDATION_FAILURE;
    *count=1;if(cap)out[0]=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;return XR_SUCCESS;
}
XrResult XRAPI_CALL layerGetViewConfigurationProperties(XrInstance instance,XrSystemId system,XrViewConfigurationType type,XrViewConfigurationProperties* out) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(s.quad&&type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        XrResult r=next.GetViewConfigurationProperties(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO,out);
        if(XR_SUCCEEDED(r))out->viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        return r;
    }
    return next.GetViewConfigurationProperties(instance,system,type,out);
}
XrResult XRAPI_CALL layerEnumerateViewConfigurationViews(XrInstance instance,XrSystemId system,XrViewConfigurationType type,uint32_t cap,uint32_t* count,XrViewConfigurationView* out) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(!s.quad||type!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)return next.EnumerateViewConfigurationViews(instance,system,type,cap,count,out);
    if(!count||(cap&&!out))return XR_ERROR_VALIDATION_FAILURE;
    *count=2;if(!cap)return XR_SUCCESS;if(cap<2)return XR_ERROR_SIZE_INSUFFICIENT;
    std::array<XrViewConfigurationView,4> runtime{};XrResult r=queryQuad(instance,system,runtime);if(XR_FAILED(r))return r;
    if(s.runtimeSettings){
        r=selectRuntimeSizes(runtime);if(XR_FAILED(r))return r;
        for(unsigned eye=0;eye<2;++eye){
            void* chain=out[eye].next;out[eye]=runtime[eye+2];out[eye].next=chain;
            out[eye].recommendedImageRectWidth=out[eye].maxImageRectWidth=s.runtimeFocusWidth;
            out[eye].recommendedImageRectHeight=out[eye].maxImageRectHeight=s.runtimeFocusHeight;
            out[eye].recommendedSwapchainSampleCount=out[eye].maxSwapchainSampleCount=1;
        }
        return XR_SUCCESS;
    }
    uint32_t width=(std::max)(runtime[2].recommendedImageRectWidth,runtime[3].recommendedImageRectWidth);
    uint32_t height=(std::max)(runtime[2].recommendedImageRectHeight,runtime[3].recommendedImageRectHeight);
    width=lmuqv::focus_dimension(width,s.outputSizes.focusPercent);height=lmuqv::focus_dimension(height,s.outputSizes.focusPercent);
    uint32_t outerWidth=lmuqv::outer_dimension(width,s.outputSizes),outerHeight=lmuqv::outer_dimension(height,s.outputSizes);
    if(!width||!height||!outerWidth||!outerHeight)return XR_ERROR_LIMIT_REACHED;
    for(unsigned i=0;i<2;++i)if(outerWidth>runtime[i].maxImageRectWidth||outerHeight>runtime[i].maxImageRectHeight)return XR_ERROR_LIMIT_REACHED;
    for(unsigned i=2;i<4;++i)if(width>runtime[i].maxImageRectWidth||height>runtime[i].maxImageRectHeight)return XR_ERROR_LIMIT_REACHED;
    double ratio=std::sqrt(s.outputSizes.outerPercent/s.outputSizes.focusPercent);
    auto outerLimit=[&](uint32_t first,uint32_t second){return uint32_t((std::min)(double(std::numeric_limits<uint32_t>::max()),std::floor((std::min)(first,second)/ratio)));};
    uint32_t maxWidth=(std::min)((std::min)(runtime[2].maxImageRectWidth,runtime[3].maxImageRectWidth),outerLimit(runtime[0].maxImageRectWidth,runtime[1].maxImageRectWidth));
    uint32_t maxHeight=(std::min)((std::min)(runtime[2].maxImageRectHeight,runtime[3].maxImageRectHeight),outerLimit(runtime[0].maxImageRectHeight,runtime[1].maxImageRectHeight));
    for(unsigned i=0;i<2;++i) {
        void* chain=out[i].next;out[i]=runtime[i+2];out[i].next=chain;
        out[i].recommendedImageRectWidth=width;out[i].recommendedImageRectHeight=height;
        out[i].maxImageRectWidth=maxWidth;out[i].maxImageRectHeight=maxHeight;
        out[i].recommendedSwapchainSampleCount=out[i].maxSwapchainSampleCount=1;
    }
    logf("Stereo-facing common INNER dimensions=%ux%u; runtime outer=%ux%u/%ux%u inner=%ux%u/%ux%u",width,height,runtime[0].recommendedImageRectWidth,runtime[0].recommendedImageRectHeight,runtime[1].recommendedImageRectWidth,runtime[1].recommendedImageRectHeight,runtime[2].recommendedImageRectWidth,runtime[2].recommendedImageRectHeight,runtime[3].recommendedImageRectWidth,runtime[3].recommendedImageRectHeight);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL layerEnumerateEnvironmentBlendModes(XrInstance instance,XrSystemId system,XrViewConfigurationType type,uint32_t cap,uint32_t* count,XrEnvironmentBlendMode* out) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    return next.EnumerateEnvironmentBlendModes(instance,system,s.quad&&type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO?XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO:type,cap,count,out);
}
XrResult XRAPI_CALL layerCreateSession(XrInstance instance,const XrSessionCreateInfo* info,XrSession* session) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    logf("xrCreateSession quad=%d",s.quad);
    if(s.session)return XR_ERROR_LIMIT_REACHED;
    if(s.quad&&s.runtimeSettings){
        std::array<XrViewConfigurationView,4> runtime{};
        XrResult sizing=queryQuad(instance,info->systemId,runtime);if(XR_FAILED(sizing))return sizing;
        sizing=selectRuntimeSizes(runtime);if(XR_FAILED(sizing))return sizing;
    }
    XrResult r=next.CreateSession(instance,info,session);if(XR_FAILED(r))return r;
    s.session=*session;s.system=info->systemId;
    for(auto chain=static_cast<const XrBaseInStructure*>(info->next);chain;chain=chain->next) {
        if(chain->type==XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
            s.device=reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(chain)->device;
            if(s.device) {s.device->AddRef();s.device->GetImmediateContext(&s.context);}break;
        }
    }
    logf("xrCreateSession result=%d D3D11=%d game=%d",r,s.device!=nullptr,gameProcess());
    if(s.device&&gameProcess()){
        const bool started=lmuqv::engine_start();logf("engine_start result=%d",started);
        if(s.runtimeSettings&&!started){
            logf("Runtime settings mode requires successful engine hooks; refusing session");
            lmuqv::engine_stop();next.DestroySession(*session);clearSession();*session=XR_NULL_HANDLE;
            return XR_ERROR_INITIALIZATION_FAILED;
        }
    }
    return r;
}
XrResult XRAPI_CALL layerDestroySession(XrSession session) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    logf("xrDestroySession");lmuqv::engine_stop();
    outerResample.reset();
    for(auto& entry:s.appChains)clearFocusRtvs(entry.second);
    destroyOuter();
    XrResult r=next.DestroySession(session);
    if(XR_SUCCEEDED(r))clearSession();
    return r;
}
XrResult XRAPI_CALL layerBeginSession(XrSession session,const XrSessionBeginInfo* info) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    auto modified=*info;if(s.quad)modified.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO;
    XrResult r=next.BeginSession(session,&modified);s.running=XR_SUCCEEDED(r);
    logf("xrBeginSession app=%d runtime=%d result=%d",info->primaryViewConfigurationType,modified.primaryViewConfigurationType,r);return r;
}
XrResult XRAPI_CALL layerEndSession(XrSession session) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);XrResult r=next.EndSession(session);
    if(XR_SUCCEEDED(r)){s.running=false;clearFrame();}logf("xrEndSession result=%d",r);return r;
}
XrResult XRAPI_CALL layerWaitFrame(XrSession session,const XrFrameWaitInfo* info,XrFrameState* state) {
    // A pipelined WaitFrame must not hold the lock needed by the previous EndFrame.
    XrResult r=next.WaitFrame(session,info,state);
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(XR_SUCCEEDED(r)){s.pendingTime=state->predictedDisplayTime;s.waitShouldRender=state->shouldRender==XR_TRUE;s.waitPending=true;}
    if(XR_FAILED(r)||s.frames<10||s.frames%120==0)logf("xrWaitFrame mode=%s completedBegins=%llu result=%d shouldRender=%d time=%lld",s.quad?"quad":"trace",static_cast<unsigned long long>(s.frames),r,s.waitShouldRender,static_cast<long long>(s.pendingTime));
    return r;
}
XrResult XRAPI_CALL layerBeginFrame(XrSession session,const XrFrameBeginInfo* info) {
    XrResult r=next.BeginFrame(session,info);
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    // Flush an abandoned/discarded frame before accepting new engine captures.
    bool released=releaseOuters();
    bool hadWait=s.waitPending;
    s.frame=XR_SUCCEEDED(r)&&s.waitPending&&released;
    if(s.waitPending){s.predictedTime=s.pendingTime;s.shouldRender=s.waitShouldRender;s.waitPending=false;}
    s.located=s.located&&s.locateTime==s.predictedTime;
    s.captures[0]=s.captures[1]=false;++s.frames;
    s.sceneFocus[0]=s.sceneFocus[1]=false;
    std::fill(std::begin(s.debugDrawn),std::end(s.debugDrawn),false);
    if(XR_FAILED(r))s.located=false;
    if(XR_FAILED(r)||!s.frame||s.frames<10||s.frames%120==0)logf("xrBeginFrame mode=%s frame=%llu pendingWait=%d renderable=%d shouldRender=%d result=%d",s.quad?"quad":"trace",static_cast<unsigned long long>(s.frames),hadWait,s.frame,s.shouldRender,r);
    return r;
}
XrResult XRAPI_CALL layerLocateViews(XrSession session,const XrViewLocateInfo* info,XrViewState* state,uint32_t cap,uint32_t* count,XrView* out) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(!s.quad||info->viewConfigurationType!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO){
        XrResult r=next.LocateViews(session,info,state,cap,count,out);
        if(XR_FAILED(r)||s.frames<10||s.frames%120==0)logf("xrLocateViews mode=%s frame=%llu capacity=%u count=%u begun=%d stateFlags=%llu time=%lld result=%d",s.quad?"quad-forward":"trace",static_cast<unsigned long long>(s.frames),cap,count?*count:0,s.frame,static_cast<unsigned long long>(XR_SUCCEEDED(r)&&state?state->viewStateFlags:0),static_cast<long long>(info->displayTime),r);
        return r;
    }
    if(!count||(cap&&!out))return XR_ERROR_VALIDATION_FAILURE;
    *count=2;if(!cap)return XR_SUCCESS;if(cap<2)return XR_ERROR_SIZE_INSUFFICIENT;
    s.located=false;
    XrViewLocateInfo modified=*info;modified.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO;
    auto foveated=structure<XrViewLocateFoveatedRenderingVARJO>(XR_TYPE_VIEW_LOCATE_FOVEATED_RENDERING_VARJO);
    foveated.next=info->next;foveated.foveatedRenderingActive=XR_TRUE;modified.next=&foveated;
    XrView runtime[4]{};for(auto& v:runtime)v.type=XR_TYPE_VIEW;
    uint32_t runtimeCount=0;XrResult r=next.LocateViews(session,&modified,state,4,&runtimeCount,runtime);
    if(XR_FAILED(r))return r;
    if(runtimeCount!=4)return XR_ERROR_RUNTIME_FAILURE;
    // One shared FOV feeds engine rendering, debug borders and final composition.
    // Preserve each runtime gaze center and pose; never constrain it to the outer FOV.
    if(!s.rawFovLogged){
        for(unsigned eye=0;eye<4;++eye)logf("Raw runtime locate FOV view=%u left=%.9g right=%.9g up=%.9g down=%.9g runtimeSettings=%d",eye,runtime[eye].fov.angleLeft,runtime[eye].fov.angleRight,runtime[eye].fov.angleUp,runtime[eye].fov.angleDown,s.runtimeSettings);
        s.rawFovLogged=true;
    }
    for(unsigned eye=2;eye<4&&!s.runtimeSettings;++eye){
        if(!lmuqv::scale_focus_fov(runtime[eye].fov,s.focusBox.width.percent,s.focusBox.height.percent)){
            logf("Invalid focus FOV eye=%u focusWidthPercent=%g focusHeightPercent=%g; rejecting located views",eye-2,s.focusBox.width.percent,s.focusBox.height.percent);
            return XR_ERROR_RUNTIME_FAILURE;
        }
    }
    const auto valid=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    s.located=(state->viewStateFlags&valid)==valid;s.locateTime=info->displayTime;s.locateSpace=info->space;
    std::copy(std::begin(runtime),std::end(runtime),std::begin(s.views));
    for(unsigned i=0;i<2;++i){out[i].pose=runtime[i].pose;out[i].fov=runtime[i].fov;}
    if(s.frames<10||s.frames%120==0) logf("xrLocateViews mode=quad frame=%llu runtime=%u app=2 begun=%d valid=%d time=%lld",static_cast<unsigned long long>(s.frames),runtimeCount,s.frame,s.located,static_cast<long long>(s.locateTime));
    return r;
}
XrResult XRAPI_CALL layerCreateSwapchain(XrSession session,const XrSwapchainCreateInfo* info,XrSwapchain* chain) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    XrResult r=next.CreateSwapchain(session,info,chain);
    logf("xrCreateSwapchain %ux%u format=%lld samples=%u array=%u usage=%llu result=%d",info->width,info->height,static_cast<long long>(info->format),info->sampleCount,info->arraySize,static_cast<unsigned long long>(info->usageFlags),r);
    if(XR_SUCCEEDED(r)){
        AppChain app{};app.info=*info;app.info.next=nullptr;
        // Color attachments can also be OSD/HUD swapchains created by an upper
        // API layer. Only the first two matching focus buffers are game eyes.
        const bool focusCandidate=(info->usageFlags&XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)&&s.nextAppEye<2&&
            (!s.runtimeSettings||(s.runtimeSizesReady&&info->width==s.runtimeFocusWidth&&info->height==s.runtimeFocusHeight&&
              info->sampleCount==1&&info->arraySize==1&&info->faceCount==1&&info->mipCount==1&&!info->createFlags));
        if(focusCandidate)app.eye=s.nextAppEye++;
        if(s.runtimeSettings)logf("Swapchain role=%s eye=%u dimensions=%ux%u",focusCandidate?"game-focus":"auxiliary",app.eye,info->width,info->height);
        s.appChains[*chain]=std::move(app);
        if(s.quad&&focusCandidate&&!createOuter(*info)&&s.runtimeSettings){
            next.DestroySwapchain(*chain);s.appChains.erase(*chain);--s.nextAppEye;*chain=XR_NULL_HANDLE;
            return XR_ERROR_RUNTIME_FAILURE;
        }
    }
    return r;
}
XrResult XRAPI_CALL layerDestroySwapchain(XrSwapchain chain) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    auto found=s.appChains.find(chain);if(found!=s.appChains.end())clearFocusRtvs(found->second);
    XrResult r=next.DestroySwapchain(chain);if(XR_SUCCEEDED(r))s.appChains.erase(chain);logf("xrDestroySwapchain result=%d",r);return r;
}
XrResult XRAPI_CALL layerEnumerateSwapchainImages(XrSwapchain chain,uint32_t cap,uint32_t* count,XrSwapchainImageBaseHeader* images) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    XrResult r=next.EnumerateSwapchainImages(chain,cap,count,images);
    auto found=s.appChains.find(chain);
    if((s.pixelEnabled||s.debugFocus)&&s.device&&XR_SUCCEEDED(r)&&cap&&count&&*count<=cap&&*count<=64&&images&&found!=s.appChains.end()&&found->second.eye<2){
        auto& app=found->second;clearFocusRtvs(app);app.images.clear();
        auto* d3d=reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
        for(uint32_t i=0;i<*count;++i){if(d3d[i].type!=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR){app.images.clear();break;}app.images.push_back(d3d[i].texture);}
    }
    logf("xrEnumerateSwapchainImages cap=%u count=%u result=%d",cap,count?*count:0,r);
    return r;
}
XrResult XRAPI_CALL layerAcquireSwapchainImage(XrSwapchain chain,const XrSwapchainImageAcquireInfo* info,uint32_t* index) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    XrResult r=next.AcquireSwapchainImage(chain,info,index);
    auto found=s.appChains.find(chain);
    if((s.pixelEnabled||s.debugFocus)&&XR_SUCCEEDED(r)&&index&&found!=s.appChains.end()&&found->second.eye<2){
        auto& app=found->second;
        if(app.acquired.size()>=64){app.trackingInvalid=true;app.acquired.clear();}
        if(!app.trackingInvalid)app.acquired.push_back({*index,false});
    }
    if(XR_FAILED(r)||s.frames<10||s.frames%120==0)logf("xrAcquireSwapchainImage index=%u result=%d",XR_SUCCEEDED(r)&&index?*index:0,r);
    return r;
}
XrResult XRAPI_CALL layerWaitSwapchainImage(XrSwapchain chain,const XrSwapchainImageWaitInfo* info) {
    // Waiting for a runtime image may block while another rendering thread finishes.
    XrResult r=next.WaitSwapchainImage(chain,info);
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    auto found=s.appChains.find(chain);
    if((s.pixelEnabled||s.debugFocus)&&r==XR_SUCCESS&&found!=s.appChains.end()&&found->second.eye<2){
        for(auto& image:found->second.acquired)if(!image.waited){image.waited=true;break;}
    }
    if(r!=XR_SUCCESS||s.frames<10||s.frames%120==0)logf("xrWaitSwapchainImage result=%d",r);
    return r;
}
XrResult XRAPI_CALL layerReleaseSwapchainImage(XrSwapchain chain,const XrSwapchainImageReleaseInfo* info) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    auto found=s.appChains.find(chain);
    if((s.pixelEnabled||s.debugFocus)&&found!=s.appChains.end()&&found->second.eye<2){
        auto& app=found->second;
        if(!app.trackingInvalid&&app.eye<2&&!app.acquired.empty()&&app.acquired.front().waited&&app.acquired.front().index<app.images.size()){
            drawAppFocusDebug(app);
            if(!s.pixelFrame&&!s.pixelDone&&s.quad&&s.frames>=30&&s.frame&&s.shouldRender&&s.located&&s.captures[0]&&s.captures[1]&&s.locateTime==s.predictedTime&&
               (!s.pixelRequireScene||(s.sceneFocus[0]&&s.sceneFocus[1]))&&
               (!s.capture.requested||lmuqv::capture_detail::triggered(s.capture))){
                s.pixelFrame=s.frames;logf("pixel_stats_begin frame=%llu requireScene=%d sceneFocus=%d/%d (single-frame readback before release)",static_cast<unsigned long long>(s.pixelFrame),s.pixelRequireScene,s.sceneFocus[0],s.sceneFocus[1]);
            }
            diagnosePixelSlot(app.eye+2,app.images[app.acquired.front().index]);
        }
    }
    XrResult r=next.ReleaseSwapchainImage(chain,info);
    if((s.pixelEnabled||s.debugFocus)&&XR_SUCCEEDED(r)&&found!=s.appChains.end()&&!found->second.acquired.empty())found->second.acquired.pop_front();
    if(XR_FAILED(r)||s.frames<10||s.frames%120==0)logf("xrReleaseSwapchainImage result=%d",r);
    return r;
}
bool validInner(const XrSwapchainSubImage& image,unsigned eye) {
    auto found=s.appChains.find(image.swapchain);if(found==s.appChains.end())return false;
    if(found->second.eye!=eye)return false;
    const auto& info=found->second.info;
    return info.format==s.colorInfo.format&&info.width==s.colorInfo.width&&info.height==s.colorInfo.height&&
           info.sampleCount==1&&image.imageArrayIndex<info.arraySize&&image.imageRect.offset.x>=0&&image.imageRect.offset.y>=0&&
           image.imageRect.extent.width>0&&image.imageRect.extent.height>0&&
           uint64_t(image.imageRect.offset.x)+image.imageRect.extent.width<=info.width&&
           uint64_t(image.imageRect.offset.y)+image.imageRect.extent.height<=info.height;
}
XrResult XRAPI_CALL layerEndFrame(XrSession session,const XrFrameEndInfo* info) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(!s.quad){
        XrResult r=next.EndFrame(session,info);
        if(XR_FAILED(r)||s.frames<10||s.frames%120==0)logf("xrEndFrame mode=trace frame=%llu appLayers=%u begun=%d shouldRender=%d time=%lld result=%d",static_cast<unsigned long long>(s.frames),info->layerCount,s.frame,s.shouldRender,static_cast<long long>(info->displayTime),r);
        clearFrame();
        return r;
    }
    // UI drawing uses the acquired RTVs. Runtime ownership resumes before submission.
    bool released=releaseOuters();
    std::vector<const XrCompositionLayerBaseHeader*> layers;
    std::vector<XrCompositionLayerProjection> projections;projections.reserve(info->layerCount);
    std::vector<std::array<XrCompositionLayerProjectionView,4>> views;views.reserve(info->layerCount);
    bool captured=released&&s.frame&&s.shouldRender&&s.located&&s.captures[0]&&s.captures[1]&&s.locateTime==info->displayTime;
    unsigned submitted=0;
    for(uint32_t i=0;i<info->layerCount;++i){
        const auto* layer=info->layers[i];
        if(!layer||layer->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION){if(layer)layers.push_back(layer);continue;}
        auto* projection=reinterpret_cast<const XrCompositionLayerProjection*>(layer);
        if(projection->viewCount!=2||!projection->views||
           !validInner(projection->views[0].subImage,0)||!validInner(projection->views[1].subImage,1)){
            if(s.runtimeSettings&&(s.frames<10||s.frames%120==0))
                logf("Projection refused: expected the two game focus swapchains at exact runtime dimensions=%ux%u; auxiliary chains and resized game buffers cannot supply focus views",s.runtimeFocusWidth,s.runtimeFocusHeight);
            continue;
        }
        if(!captured||projection->space!=s.locateSpace)continue;
        // One engine scene capture pair cannot represent a second projection layer.
        if(submitted)continue;
        views.emplace_back();auto& v=views.back();
        for(unsigned eye=0;eye<2;++eye){
            v[eye]=structure<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW);
            v[eye].pose=s.views[eye].pose;v[eye].fov=s.views[eye].fov;
            v[eye].subImage.swapchain=s.outer[eye].chain;
            v[eye].subImage.imageRect.extent={static_cast<int32_t>(s.outer[eye].width),static_cast<int32_t>(s.outer[eye].height)};
            v[eye+2]=projection->views[eye];v[eye+2].pose=s.views[eye+2].pose;v[eye+2].fov=s.views[eye+2].fov;
        }
        projections.push_back(*projection);auto& p=projections.back();p.viewCount=4;p.views=v.data();
        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&p));++submitted;
    }
    auto modified=*info;modified.layerCount=static_cast<uint32_t>(layers.size());modified.layers=layers.empty()?nullptr:layers.data();
    XrResult r=next.EndFrame(session,&modified);
    if(s.pixelEnabled&&!s.pixelDone&&s.pixelFrame==s.frames&&s.pixelFrame){
        logf("pixel_stats_complete mask=%u successMask=%u frame=%llu quadProjections=%u releaseOkay=%d result=%d captureRequested=%d captureSuccessMask=%u",s.pixelMask,s.pixelSuccessMask,static_cast<unsigned long long>(s.pixelFrame),submitted,released,r,s.capture.requested,s.captureMask);s.pixelDone=true;
    }
    if(s.frames<10||s.frames%120==0||(!submitted&&s.shouldRender))logf("xrEndFrame frame=%llu quadProjections=%u captures=%d/%d located=%d shouldRender=%d result=%d",static_cast<unsigned long long>(s.frames),submitted,s.captures[0],s.captures[1],s.located,s.shouldRender,r);
    clearFrame();return r;
}
XrResult XRAPI_CALL layerGetInstanceProcAddr(XrInstance instance,const char* name,PFN_xrVoidFunction* function) {
    if(!name||!function)return XR_ERROR_VALIDATION_FAILURE;
    logf("xrGetInstanceProcAddr %s",name);
#define INTERCEPT(api) if(!std::strcmp(name,"xr" #api)){*function=reinterpret_cast<PFN_xrVoidFunction>(layer##api);return XR_SUCCESS;}
    INTERCEPT(GetInstanceProcAddr) INTERCEPT(DestroyInstance) INTERCEPT(EnumerateViewConfigurations)
    INTERCEPT(GetViewConfigurationProperties) INTERCEPT(EnumerateViewConfigurationViews) INTERCEPT(EnumerateEnvironmentBlendModes)
    INTERCEPT(CreateSession) INTERCEPT(DestroySession) INTERCEPT(BeginSession) INTERCEPT(EndSession)
    INTERCEPT(WaitFrame) INTERCEPT(BeginFrame) INTERCEPT(EndFrame) INTERCEPT(LocateViews)
    INTERCEPT(CreateSwapchain) INTERCEPT(DestroySwapchain)
    INTERCEPT(EnumerateSwapchainImages) INTERCEPT(AcquireSwapchainImage) INTERCEPT(WaitSwapchainImage) INTERCEPT(ReleaseSwapchainImage)
#undef INTERCEPT
    return next.get?next.get(instance,name,function):XR_ERROR_FUNCTION_UNSUPPORTED;
}
XrResult XRAPI_CALL layerCreateApiLayerInstance(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(!info||!layer||!instance||layer->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO||
       layer->structVersion!=XR_API_LAYER_CREATE_INFO_STRUCT_VERSION||layer->structSize!=sizeof(XrApiLayerCreateInfo)||
       !layer->nextInfo)return XR_ERROR_INITIALIZATION_FAILED;
    auto* chain=layer->nextInfo;
    if(chain->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO||chain->structVersion!=XR_API_LAYER_NEXT_INFO_STRUCT_VERSION||
       chain->structSize!=sizeof(XrApiLayerNextInfo)||std::strcmp(chain->layerName,layerName)||
       !chain->nextGetInstanceProcAddr||!chain->nextCreateApiLayerInstance)return XR_ERROR_INITIALIZATION_FAILED;
    if(s.instance)return XR_ERROR_LIMIT_REACHED;
    bool quad=quadMode();auto modified=*info;std::vector<const char*> extensions;
    for(uint32_t i=0;i<info->enabledExtensionCount;++i)extensions.push_back(info->enabledExtensionNames[i]);
    if(quad)for(const char* extension:{XR_VARJO_QUAD_VIEWS_EXTENSION_NAME,XR_VARJO_FOVEATED_RENDERING_EXTENSION_NAME}){
        if(std::none_of(extensions.begin(),extensions.end(),[&](const char* e){return !std::strcmp(e,extension);}))extensions.push_back(extension);
    }
    modified.enabledExtensionCount=static_cast<uint32_t>(extensions.size());modified.enabledExtensionNames=extensions.data();
    auto downstream=*layer;downstream.nextInfo=chain->next;
    XrResult r=chain->nextCreateApiLayerInstance(&modified,&downstream,instance);
    logf("xrCreateApiLayerInstance mode=%s app=%s extensions=%u result=%d",quad?"quad":"trace",info->applicationInfo.applicationName,modified.enabledExtensionCount,r);
    if(XR_FAILED(r))return r;
    next.get=chain->nextGetInstanceProcAddr;
    if(!next.load(*instance)){if(next.DestroyInstance)next.DestroyInstance(*instance);*instance=XR_NULL_HANDLE;return XR_ERROR_INITIALIZATION_FAILED;}
    s.instance=*instance;s.quad=quad;
    char runtimeSetting[8]{};GetEnvironmentVariableA("LMU_QV_USE_RUNTIME_SETTINGS",runtimeSetting,sizeof(runtimeSetting));
    s.runtimeSettings=quad&&!std::strcmp(runtimeSetting,"1");
    if(s.runtimeSettings)logf("Runtime settings enabled: runtime FOV unchanged; exact runtime outer and common focus dimensions; legacy percentage controls ignored");
    if(!s.runtimeSettings){
    char outerPercent[128]{},focusPercent[128]{};
    DWORD outerLength=GetEnvironmentVariableA("LMU_QV_OUTER_PIXELS_PERCENT",outerPercent,sizeof(outerPercent));
    DWORD focusLength=GetEnvironmentVariableA("LMU_QV_FOCUS_PIXELS_PERCENT",focusPercent,sizeof(focusPercent));
    s.outputSizes=lmuqv::parse_output_sizes(outerLength>=sizeof(outerPercent)?"invalid":outerPercent,focusLength>=sizeof(focusPercent)?"invalid":focusPercent);
    if(!s.outputSizes.valid)logf("Invalid output percentages; using safe defaults outer=100 focus=100 (outer range 1..400, focus range 25..400, outer<=focus)");
    if(quad)logf("Output pixel percentages outer=%g focus=%g (relative to original common INNER recommendation)",s.outputSizes.outerPercent,s.outputSizes.focusPercent);
    char boxPercent[128]{},widthPercent[128]{},heightPercent[128]{};
    DWORD boxLength=GetEnvironmentVariableA("LMU_QV_FOCUS_BOX_PERCENT",boxPercent,sizeof(boxPercent));
    DWORD widthLength=GetEnvironmentVariableA("LMU_QV_FOCUS_WIDTH_PERCENT",widthPercent,sizeof(widthPercent));
    DWORD heightLength=GetEnvironmentVariableA("LMU_QV_FOCUS_HEIGHT_PERCENT",heightPercent,sizeof(heightPercent));
    s.focusBox=lmuqv::parse_focus_box_size(widthLength>=sizeof(widthPercent)?"invalid":widthPercent,
        heightLength>=sizeof(heightPercent)?"invalid":heightPercent,boxLength>=sizeof(boxPercent)?"invalid":boxPercent);
    if(!s.focusBox.width.valid)logf("Invalid focus width percentage; using safe default 100 (decimal range 25..200)");
    if(!s.focusBox.height.valid)logf("Invalid focus height percentage; using safe default 100 (decimal range 25..200)");
    if(quad)logf("Focus box width=%g%% height=%g%% (tangent-space extents around runtime focus center)",s.focusBox.width.percent,s.focusBox.height.percent);
    }
    char pixelEnv[8]{};GetEnvironmentVariableA("LMU_QV_PIXEL_STATS",pixelEnv,sizeof(pixelEnv));
    s.pixelRequireScene=quad&&!std::strcmp(pixelEnv,"scene");
    s.pixelEnabled=quad&&(!std::strcmp(pixelEnv,"1")||s.pixelRequireScene);
    s.capture=lmuqv::capture_detail::config(environmentWide(L"LMU_QV_CAPTURE_DIR"),environmentWide(L"LMU_QV_CAPTURE_TRIGGER"));
    if(quad&&s.capture.requested){
        s.pixelEnabled=true;s.pixelRequireScene=true;
        logf("capture_config valid=%d existingDirectory=%s trigger=%s (four fixed BMP names; CREATE_NEW; scene gate)",s.capture.valid,s.capture.directory.u8string().c_str(),s.capture.trigger.u8string().c_str());
    }
    if(s.pixelEnabled)logf("pixel_stats_enabled minimumFrame=30 oneFrame=1 requireScene=%d",s.pixelRequireScene);
    char debugEnv[8]{};GetEnvironmentVariableA("LMU_QV_DEBUG_FOCUS",debugEnv,sizeof(debugEnv));s.debugFocus=quad&&!std::strcmp(debugEnv,"1");
    if(s.debugFocus)logf("debug_focus enabled: lime runtime Focus-FOV outline; current cached FOV, not a synthetic gaze signal");
    return r;
}
} // namespace

namespace lmuqv {
void xr_scene_focus_complete(unsigned eye) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(eye>1||!s.quad||!s.frame||!s.shouldRender||!s.located||s.locateTime!=s.predictedTime)return;
    s.sceneFocus[eye]=true;
    if(s.frames<10||s.frames%120==0||(s.pixelRequireScene&&!s.pixelDone&&s.sceneFocus[0]&&s.sceneFocus[1]))
        logf("scene_focus_complete frame=%llu eye=%u completed=%d/%d",static_cast<unsigned long long>(s.frames),eye,s.sceneFocus[0],s.sceneFocus[1]);
}
ID3D11RenderTargetView* xr_outer_rtv(unsigned eye) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(eye>1||!s.frame||!s.shouldRender||!s.captures[eye])return nullptr;
    const auto& outer=s.outer[eye];
    if(!outer.acquired||!outer.waited||outer.index>=outer.rtvs.size())return nullptr;
    return outer.rtvs[outer.index];
}
void log_line(const char* message) {
    std::lock_guard<std::mutex> lock(logMutex);
    static HANDLE file=[](){
        wchar_t path[32768]{};DWORD n=GetEnvironmentVariableW(L"LMU_QV_LOG",path,32768);
        std::filesystem::path target;
        if(n&&n<32768&&std::filesystem::path(path).is_absolute())target=path;
        else {
            HMODULE self{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&log_line),&self);
            GetModuleFileNameW(self,path,32768);auto directory=std::filesystem::path(path).parent_path();
            if(directory.filename()==L"build")directory=directory.parent_path();
            target=directory/L"logs"/L"xr-layer.log";
        }
        std::error_code error;std::filesystem::create_directories(target.parent_path(),error);
        return CreateFileW(target.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    }();
    if(file==INVALID_HANDLE_VALUE)return;
    char line[2304]{};SYSTEMTIME now{};GetSystemTime(&now);
    int count=std::snprintf(line,sizeof(line),"%02u:%02u:%02u.%03u [%lu] %s\r\n",now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentThreadId(),message?message:"");
    DWORD written{};WriteFile(file,line,static_cast<DWORD>((std::min)(count,int(sizeof(line)-1))),&written,nullptr);
}
bool xr_runtime_sizes(unsigned eye,uint32_t& outerWidth,uint32_t& outerHeight,uint32_t& focusWidth,uint32_t& focusHeight){
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(eye>1||!s.runtimeSettings||!s.runtimeSizesReady)return false;
    outerWidth=s.runtimeOuterWidth[eye];outerHeight=s.runtimeOuterHeight[eye];focusWidth=s.runtimeFocusWidth;focusHeight=s.runtimeFocusHeight;return true;
}
bool xr_quad_active(){std::lock_guard<std::recursive_mutex> lock(stateMutex);return s.quad&&s.session&&s.running;}
bool xr_frame_views(XrView (&views)[4]) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(!s.quad||!s.frame||!s.shouldRender||!s.located||s.locateTime!=s.predictedTime)return false;
    std::copy(std::begin(s.views),std::end(s.views),std::begin(views));return true;
}
bool xr_copy_outer(unsigned eye,ID3D11Texture2D* source) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if(eye>1||!source||!s.quad||!s.frame||!s.shouldRender||!s.located||!s.context||s.captures[eye])return false;
    auto& outer=s.outer[eye];if(!outer.chain)return false;
    D3D11_TEXTURE2D_DESC src{};source->GetDesc(&src);
    const bool fullSize=src.Width==s.colorInfo.width&&src.Height==s.colorInfo.height;
    const bool nativeOuterSize=src.Width==outer.width&&src.Height==outer.height;
    if((!fullSize&&!nativeOuterSize)||src.SampleDesc.Count!=1||src.ArraySize!=1||src.MipLevels!=1||formatFamily(src.Format)!=formatFamily(static_cast<DXGI_FORMAT>(s.colorInfo.format))) {
        logf("outer copy incompatible eye=%u source=%ux%u fmt=%u samples=%u array=%u mips=%u expected=%ux%u fmt=%lld",eye,src.Width,src.Height,src.Format,src.SampleDesc.Count,src.ArraySize,src.MipLevels,s.colorInfo.width,s.colorInfo.height,static_cast<long long>(s.colorInfo.format));return false;
    }
    bool resized=src.Width!=outer.width||src.Height!=outer.height;
    bool resampleFormat=src.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||src.Format==DXGI_FORMAT_R8G8B8A8_UNORM||src.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||
                        src.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS||src.Format==DXGI_FORMAT_B8G8R8A8_UNORM||src.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    if(resized&&(!(src.BindFlags&D3D11_BIND_SHADER_RESOURCE)||!resampleFormat)){
        logf("outer resample incompatible source eye=%u format=%u bindFlags=%u",eye,src.Format,src.BindFlags);return false;
    }
    ID3D11Device* sourceDevice{};source->GetDevice(&sourceDevice);bool same=sourceDevice==s.device;if(sourceDevice)sourceDevice->Release();if(!same){logf("outer copy rejected different device");return false;}
    if(!outer.acquired){auto acquire=structure<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);XrResult r=next.AcquireSwapchainImage(outer.chain,&acquire,&outer.index);if(XR_FAILED(r)){logf("acquire outer %u result=%d",eye,r);return false;}outer.acquired=true;}
    if(!outer.waited){auto wait=structure<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);wait.timeout=XR_INFINITE_DURATION;XrResult r=next.WaitSwapchainImage(outer.chain,&wait);if(r!=XR_SUCCESS){logf("wait outer %u result=%d (acquisition retained)",eye,r);return false;}outer.waited=true;}
    bool copied=false;
    if(outer.index<outer.images.size()&&outer.images[outer.index].texture){
        ID3D11Texture2D* destination=outer.images[outer.index].texture;D3D11_TEXTURE2D_DESC dst{};destination->GetDesc(&dst);
        if(dst.Width==outer.width&&dst.Height==outer.height&&dst.ArraySize==src.ArraySize&&dst.MipLevels==src.MipLevels&&dst.SampleDesc.Count==1&&formatFamily(dst.Format)==formatFamily(src.Format)){
            if(!resized){s.context->CopyResource(destination,source);copied=SUCCEEDED(s.device->GetDeviceRemovedReason());}
            else if(outer.index<outer.rtvs.size())copied=outerResample.draw(s.context,source,outer.rtvs[outer.index],outer.width,outer.height);
        }
    }
    s.captures[eye]=copied;
    if(s.frames<10||s.frames%120==0)logf("outer capture eye=%u copied=%d resized=%d output=%ux%u (image held for UI)",eye,copied,resized,outer.width,outer.height);
    return s.captures[eye];
}
} // namespace lmuqv

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader,const char* name,XrNegotiateApiLayerRequest* request) {
    if(!loader||!name||std::strcmp(name,layerName)||!request||
       loader->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO||loader->structVersion!=XR_LOADER_INFO_STRUCT_VERSION||loader->structSize!=sizeof(XrNegotiateLoaderInfo)||
       request->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST||request->structVersion!=XR_API_LAYER_INFO_STRUCT_VERSION||request->structSize!=sizeof(XrNegotiateApiLayerRequest)||
       loader->minInterfaceVersion>1||loader->maxInterfaceVersion<1||loader->minApiVersion>XR_CURRENT_API_VERSION||loader->maxApiVersion<XR_MAKE_VERSION(1,0,0))return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion=1;request->layerApiVersion=(std::min)(loader->maxApiVersion,XR_CURRENT_API_VERSION);
    request->getInstanceProcAddr=layerGetInstanceProcAddr;request->createApiLayerInstance=layerCreateApiLayerInstance;
    logf("Loader negotiated API layer interface v1");return XR_SUCCESS;
}
