// White-box tests of the native hook policy. No LMU process or native game code is used.
#include "../engine_bridge.cpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace lmuqv {
bool xr_runtime_sizes(unsigned eye,uint32_t& width,uint32_t& height,uint32_t& focusWidth,uint32_t& focusHeight){
    if(eye>1)return false;
    width=eye?20:24;height=eye?28:16;focusWidth=focusHeight=32;return true;
}
bool xr_quad_active() {return false;}
bool xr_frame_views(XrView (&)[4]) {return false;}
bool xr_copy_outer(unsigned,ID3D11Texture2D*) {return false;}
void xr_scene_focus_complete(unsigned) {}
ID3D11RenderTargetView* xr_outer_rtv(unsigned) {return nullptr;}
void log_line(const char*) {}
void flush_log(){}
}
namespace {
unsigned masks=0,derived=0;
unsigned lastFlags=0;
bool lastSuppress=false;
void require(bool value,const char* message) {
    if(!value) {std::cerr<<message<<'\n';std::exit(1);}
}
void setProjection(void* ptr,const float* p) {
    auto* view=static_cast<lmuqv::ViewSnapshot*>(ptr);
    std::copy_n(p,16,view->projection.begin());view->flags|=0x100000;
}
void derive(void* ptr,unsigned flags,bool suppress) {
    ++derived;lastFlags=flags;lastSuppress=suppress;
    auto* view=static_cast<lmuqv::ViewSnapshot*>(ptr);
    if((flags&4) && (view->flags&0x200000))view->projection.fill(99);
}
void mask(void*,void*) {++masks;}

}
int main() {
    using namespace lmuqv;
    unsigned nativeWidth=0,nativeHeight=0;
    nativeRuntimeSettings=true;
    nativeOuterSizes=parse_output_sizes("10","200");
    require(nativeOuterDimensions(0,32,32,nativeWidth,nativeHeight)&&nativeWidth==24&&nativeHeight==16,"runtime left target used percentages");
    require(nativeOuterDimensions(1,32,32,nativeWidth,nativeHeight)&&nativeWidth==20&&nativeHeight==28,"runtime right target lost asymmetric dimensions");
    require(!nativeOuterDimensions(0,31,32,nativeWidth,nativeHeight),"LMU input scaling must reject mismatched native target size");
    nativeRuntimeSettings=false;
    require(nativeOuterDimensions(0,32,32,nativeWidth,nativeHeight)&&nativeWidth==8&&nativeHeight==8,"legacy native percentage sizing changed");
    // Ordering guard requested for the nested-render restoration fix; no NVAPI/GPU execution.
    std::string engineSource;
    for(const char* path:{"engine_bridge.cpp","mod/engine_bridge.cpp","../engine_bridge.cpp"}){
        std::ifstream input(path);
        if(input){engineSource.assign(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());break;}
    }
    const std::string hookSignature="void renderHook(void* unknown, bool laterPass) {";
    auto hookStart=engineSource.find(hookSignature);
    require(hookStart!=std::string::npos,"renderHook source unavailable for nested-scope ordering guard");
    auto firstStatement=engineSource.find_first_not_of(" \t\r\n",hookStart+hookSignature.size());
    require(engineSource.compare(firstStatement,std::strlen("suspendUnexpectedVrs();"),"suspendUnexpectedVrs();")==0,
            "active parent VRS must suspend before renderHook timing and TLS changes");
    unsigned defaultRate=99;
    require(parseVrsDefaultRate(false,"4",1,defaultRate)&&defaultRate==1,"VRS default enabled without opt-in");
    require(parseVrsDefaultRate(true,nullptr,0,defaultRate)&&defaultRate==2,"opt-in default is not 2x2");
    for(char rate : {'1','2','4'}) {
        require(parseVrsDefaultRate(true,&rate,1,defaultRate)&&defaultRate==unsigned(rate-'0'),
                "valid explicit VRS default rejected");
    }
    for(const char* invalid : {"0","3","5"," 2","2 ","02","NaN"})
        require(!parseVrsDefaultRate(true,invalid,std::strlen(invalid),defaultRate),"invalid VRS default accepted");
    worldVrsDefaultRate=4;
    require(benchmarkRequestedRate()==4,"non-benchmark VRS default ignored");
    benchmarkPath=L"test-control";
    require(benchmarkRequestedRate()==1,"benchmark baseline replaced by non-benchmark VRS default");
    benchmarkPath.clear();worldVrsDefaultRate=1;
    GpuTimingStats completeGpu{};PipelineStatsResult completeWork{};
    completeGpu.done=completeWork.done=true;completeGpu.samples=completeWork.samples=60;
    require(benchmarkSlotComplete(completeGpu,completeWork),"complete benchmark slot rejected");
    for(unsigned count : {0u,59u,61u}) {
        auto partial=completeGpu;partial.samples=count;
        require(!benchmarkSlotComplete(partial,completeWork),"incomplete GPU samples accepted");
        auto partialWork=completeWork;partialWork.samples=count;
        require(!benchmarkSlotComplete(completeGpu,partialWork),"incomplete pipeline samples accepted");
    }
    completeGpu.done=false;
    require(!benchmarkSlotComplete(completeGpu,completeWork),"unfinished GPU slot accepted");
    completeGpu.done=true;completeWork.done=false;
    require(!benchmarkSlotComplete(completeGpu,completeWork),"unfinished pipeline slot accepted");

    projectionOriginal=setProjection;derivedOriginal=derive;maskOriginal=mask;
    ViewSnapshot selected{},child{};
    Matrix outer{},inner{};outer[0]=1;inner[0]=4;
    selected.flags=0x200042;
    projectionHook(&selected,outer.data());
    require(selected.projection==outer,"ordinary projection changed");
    focusView=&selected;focusProjection=&inner;
    projectionHook(&selected,outer.data());
    projectionHook(&child,outer.data());
    require(selected.projection==inner,"main-pass override defeated focus projection");
    require(child.projection==outer,"child projection modified by focus policy");
    Matrix cockpitClip=outer;cockpitClip[10]=-0.0001f;cockpitClip[14]=0.01f;cockpitClip[11]=1;
    projectionHook(&selected,cockpitClip.data());
    require(selected.projection[0]==inner[0]&&selected.projection[10]==cockpitClip[10]&&
            selected.projection[14]==cockpitClip[14],"cockpit subpass depth parameters overwritten");
    derivedHook(&selected,6,false);
    require(selected.projection[10]==cockpitClip[10]&&selected.projection[14]==cockpitClip[14],
            "derived hook overwrote cockpit clipping");
    projectionHook(&selected,outer.data());
    derivedHook(&selected,6,false);
    require(selected.projection==inner,"derived projection regeneration defeated focus");
    require(selected.flags==0x100042,"unrelated flags changed");
    require(lastFlags==6&&!lastSuppress,"derived call arguments changed");
    child.flags|=0x200000;
    derivedHook(&child,4,true);
    require(child.projection[0]==99&&lastSuppress,"child generation not passed through");
    maskHook(nullptr,&selected);maskHook(nullptr,&child);
    require(masks==1&&skippedMasks==1,"mask suppression leaked to another view");
    focusView=nullptr;focusProjection=nullptr;
    projectionHook(&selected,outer.data());derivedHook(&selected,6,false);
    maskHook(nullptr,&selected);
    require(selected.projection==outer&&masks==2&&derived==4,"outer restoration still intercepted");
    XrPosef a{},b{};a.orientation.w=1;b.orientation.w=-1;
    require(equalPose(a,b),"equivalent opposite-sign quaternion rejected");
    b.position.x=0.01f;
    require(!equalPose(a,b),"different physical pose accepted");
    History remembered{&selected,{},true};
    remembered.transform.matrix.fill(7.f);
    prepareHistory(remembered,&selected,true);
    require(remembered.valid&&remembered.transform.matrix[0]==7.f,
            "continuous native camera history discarded");
    prepareHistory(remembered,&selected,false);
    require(!remembered.valid&&remembered.owner==&selected,
            "native camera-cut invalidation ignored for same View pointer");
    remembered.valid=true;remembered.transform.matrix.fill(9.f);
    prepareHistory(remembered,&child,true);
    require(!remembered.valid&&remembered.owner==&child,
            "focus history reused by a different View owner");
    auto* originalTarget=reinterpret_cast<ID3D11RenderTargetView*>(std::uintptr_t(0x100));
    auto* smallTarget=reinterpret_cast<ID3D11RenderTargetView*>(std::uintptr_t(0x200));
    auto* targetSlot=originalTarget;UINT dimensions[]{3380,3432};
    {
        UiTargetScope scope(targetSlot,dimensions,smallTarget,756,768);
        require(targetSlot==smallTarget&&dimensions[0]==756&&dimensions[1]==768,
                "outer UI target and viewport did not change together");
    }
    require(targetSlot==originalTarget&&dimensions[0]==3380&&dimensions[1]==3432,
            "outer UI target override leaked into native focus UI");
    nativeOuterActive=&selected;nativeOuterProjection=&outer;
    Matrix changed=inner;changed[10]=0.031f;changed[14]=0.021f;
    projectionHook(&selected,changed.data());
    require(selected.projection[0]==outer[0]&&selected.projection[10]==changed[10]&&selected.projection[14]==changed[14],
            "native small viewport changed outer FOV or subpass depth");
    projectionHook(&child,inner.data());
    require(child.projection==inner,"native outer projection leaked to child view");
    nativeOuterActive=nullptr;nativeOuterProjection=nullptr;
    std::cout<<"engine hook policy tests passed\n";
}
