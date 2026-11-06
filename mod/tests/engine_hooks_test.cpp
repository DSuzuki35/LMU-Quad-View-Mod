// White-box tests of the native hook policy. No LMU process or native game code is used.
#include "../engine_bridge.cpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <fstream>

namespace lmuqv {
bool testQuadFrame=false,testFullFovAvailable=true,testUiScenePresent=false;
XrView testFrameViews[4]{};
XrFovf testFullFov[2]{};
bool xr_runtime_sizes(unsigned eye,uint32_t& width,uint32_t& height,uint32_t& focusWidth,uint32_t& focusHeight){
    if(eye>1)return false;
    width=eye?20:24;height=eye?28:16;focusWidth=focusHeight=32;return true;
}
bool xr_vrs_active(){return false;}
bool xr_vrs_focus(unsigned,VrsFocus&){return false;}
bool xr_vrs_render_focus(unsigned,const float*,VrsFocus&){return false;}
void xr_vrs_scene_complete(unsigned){}
bool xr_quad_active() {return testQuadFrame;}
bool xr_frame_views(XrView (&views)[4]) {
    if(!testQuadFrame)return false;
    std::copy_n(testFrameViews,4,views);return true;
}
bool xr_frame_inputs(QuadFrameInputs& out){out={};return false;}
bool xr_frame_full_outer_fov(XrFovf (&fov)[2]) {
    if(!testQuadFrame||!testFullFovAvailable)return false;
    std::copy_n(testFullFov,2,fov);return true;
}
bool xr_copy_outer(unsigned,ID3D11Texture2D*) {return false;}
void xr_scene_focus_complete(unsigned) {}
ID3D11RenderTargetView* xr_outer_rtv(unsigned) {
    return testUiScenePresent?reinterpret_cast<ID3D11RenderTargetView*>(std::uintptr_t(1)):nullptr;
}
OuterTarget xr_outer_target(unsigned) {return {};}
void log_line(const char*) {}
void flush_log(){}
}
namespace {
unsigned masks=0,derived=0;
unsigned ownerForwardCalls=0;
unsigned cameraStateForwards=0,cameraPoseForwards=0;
constexpr uint32_t cameraPoseBits[]{0x7fc12345u,0x80000000u,0x3f800000u,0xbf000000u,0x00000000u,0x40000000u};
void cameraStateForward(void* renderer,void* destination){
    assert(renderer==reinterpret_cast<void*>(1)&&destination==reinterpret_cast<void*>(2));
    ++cameraStateForwards;_mm_setcsr(0x1fbf);
}
void cameraPoseForward(void* view,float x,float y,float z,float pitch,float yaw,float roll){
    assert(view==reinterpret_cast<void*>(3));
    const float values[]{x,y,z,pitch,yaw,roll};
    for(unsigned i=0;i<6;++i){uint32_t bits=0;std::memcpy(&bits,&values[i],4);assert(bits==cameraPoseBits[i]);}
    ++cameraPoseForwards;_mm_setcsr(0x1fbf);
}
void ownerForward(void* a,void* b,void* c,float bias){
    uint32_t bits=0;std::memcpy(&bits,&bias,sizeof(bits));
    assert(a==reinterpret_cast<void*>(1)&&b==reinterpret_cast<void*>(2)&&c==reinterpret_cast<void*>(3)&&bits==0x7fc12345u);
    ++ownerForwardCalls;_mm_setcsr(0x1fbf);
}
float expectedUiPlaneFov=0,expectedUiScale=0;
unsigned uiPlaneDraws=0;
void checkUiPlane(void* object,ID3D11DeviceContext*,const lmuqv::UiArgs* args){
    float value{};std::memcpy(&value,static_cast<const std::byte*>(object)+0xc4,sizeof(value));
    if(std::abs(value-expectedUiPlaneFov)>1e-6f){
        std::cerr<<"UI plane FOV must retain its baked mesh curvature basis\n";std::exit(1);
    }
    std::memcpy(&value,static_cast<const std::byte*>(object)+(args->mode==1?0x100:0xf8),sizeof(value));
    if(std::abs(value-expectedUiScale)>1e-6f){
        std::cerr<<"cropped UI must retain full-FOV XY size: got scale "<<value<<" expected "<<expectedUiScale<<'\n';std::exit(1);
    }
    ++uiPlaneDraws;
}
unsigned lastFlags=0;
bool lastSuppress=false;
void require(bool value,const char* message) {
    if(!value) {std::cerr<<message<<'\n';std::exit(1);}
}
void uiPlaneSizeTest(){
    using namespace lmuqv;
    std::array<std::byte,0x108> object{};
    // Native settings are independent scale/depth values, not part of the FOV correction.
    const float settings[]{2560,1440,.8f,.5f,1.f,.5f};
    for(unsigned i=0;i<6;++i){
        const size_t offsets[]{0x88,0x8c,0xf8,0xfc,0x100,0x104};
        std::memcpy(object.data()+offsets[i],&settings[i],sizeof(float));
    }
    testFullFov[0]={-1.05546f,.84253f,.58247751f,-.58247751f};
    testFullFov[1]={-.84253f,1.05546f,.58247751f,-.58247751f};
    for(unsigned eye=0;eye<2;++eye){
        testFrameViews[eye].fov={testFullFov[eye].angleLeft,testFullFov[eye].angleRight,.30023717f,-.30023717f};
        testFrameViews[eye+2].fov={-.2f,.2f,.2f,-.2f};
    }
    const float croppedSpan=testFrameViews[0].fov.angleUp-testFrameViews[0].fov.angleDown;
    std::memcpy(object.data()+0xc4,&croppedSpan,sizeof(float));
    const auto original=object;
    UiArgs args{};args.mode=2;
    uiOriginal=checkUiPlane;testQuadFrame=true;nativeRuntimeSettings=true;
    expectedUiPlaneFov=croppedSpan;
    const float ratio=std::tan((testFullFov[0].angleUp-testFullFov[0].angleDown)*.5f)/std::tan(croppedSpan*.5f);
    expectedUiScale=.8f*ratio;
    uiHook(object.data(),nullptr,&args);
    require(object==original,"UI angular plane override leaked into game state or changed user preferences");
    args.mode=1;expectedUiScale=ratio;
    uiHook(object.data(),nullptr,&args);
    require(object==original,"HUD plane override leaked into native settings");
    args.mode=3;expectedUiScale=.8f*ratio;testUiScenePresent=true;
    uiHook(object.data(),nullptr,&args);
    require(object==original,"mode3 plane override leaked into native settings");
    args.mode=2;testUiScenePresent=false;
    // Projection validation fallback must also unwind the temporary field override.
    testFrameViews[2].fov={};
    uiHook(object.data(),nullptr,&args);
    require(object==original,"UI projection fallback leaked the plane override");
    testFullFovAvailable=false;expectedUiScale=.8f;
    uiHook(object.data(),nullptr,&args);
    require(object==original,"unavailable full FOV mutated native UI state");
    testQuadFrame=false;testFullFovAvailable=true;uiOriginal=nullptr;nativeRuntimeSettings=false;
    require(uiPlaneDraws==5,"UI plane fixture did not exercise all native calls");
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

bool expectedUploadScope=false,enterNestedUploadScope=false;
unsigned uploadSortCalls=0;
void uploadSort(void* renderer,void* scene,void* view,std::int32_t group,
                std::uint32_t flags,bool lastMainPass,void* material){
    using namespace lmuqv;
    ++uploadSortCalls;
    auto* cache=static_cast<std::byte*>(renderer)+0x77fc8;
    const auto token=nativeUploadReuse.before(cache,selectedUploadContext);
    require(token.valid==expectedUploadScope,"upload reuse scope eligibility changed");
    if(expectedUploadScope)require(selectedUploadCache==cache,"wrong native upload cache selected");
    if(enterNestedUploadScope){
        enterNestedUploadScope=false;expectedUploadScope=false;
        worldSortHook(renderer,scene,view,group,flags,lastMainPass,material);
        require(!nativeUploadReuse.before(cache,selectedUploadContext).valid,
                "nested group incorrectly resumed parent reuse assumptions");
    }
}
void outerUploadScopeTest(){
    using namespace lmuqv;
    // Only readability and pointer identity are needed; no D3D methods are called
    // by this fixture. All seven native buffer fields remain null.
    std::vector<std::byte> renderer(0x7b000);
    std::uintptr_t contextStorage=0;
    auto* context=reinterpret_cast<ID3D11DeviceContext*>(&contextStorage);
    std::memcpy(renderer.data()+0x76f68,&context,sizeof(context));
    ViewSnapshot outer{},other{};
    worldSortOriginal=uploadSort;nativeUploadHooked=true;nativeUploadOptIn=true;
    nativeUploadOuterOptIn=true;nativeUploadBatchOptIn=false;nativeOuterExpected=&outer;
    auto run=[&](void* view,int group,bool eligible){
        expectedUploadScope=eligible;
        worldSortHook(renderer.data(),nullptr,view,group,0,false,nullptr);
        require(!selectedUploadCache&&!selectedUploadContext,"upload scope escaped world sort");
        require(!nativeUploadReuse.before(renderer.data()+0x77fc8,context).valid,
                "GPU content assumptions survived a completed render group");
    };
    run(&outer,31,true);run(&other,31,false);run(&outer,30,false);run(nullptr,31,false);
    nativeUploadOuterOptIn=false;run(&outer,31,false);
    focusView=&outer;run(&outer,31,true);focusView=nullptr;
    nativeUploadOuterOptIn=true;nativeUploadHooked=false;run(&outer,31,false);nativeUploadHooked=true;
    enterNestedUploadScope=true;run(&outer,31,true);
    require(uploadSortCalls==9,"native world sort calls changed");
    nativeUploadOptIn=false;nativeUploadOuterOptIn=false;nativeUploadHooked=false;
    nativeOuterExpected=nullptr;worldSortOriginal=nullptr;nativeUploadReuse.reset();
}
void outerUploadSettingsTest(){
    using namespace lmuqv;
    const char* names[]{"LMU_QV_MODE","LMU_QV_CPU_UPLOAD_REUSE","LMU_QV_CPU_OUTER_REUSE","LMU_QV_CPU_UPLOAD_BATCH"};
    std::array<std::string,4> prior{};
    for(size_t i=0;i<prior.size();++i){char value[32768]{};GetEnvironmentVariableA(names[i],value,sizeof(value));prior[i]=value;SetEnvironmentVariableA(names[i],nullptr);}
    SetEnvironmentVariableA(names[0],"trace");
    require(engine_start(nullptr)&&!nativeUploadOptIn&&!nativeUploadOuterOptIn,"CPU experiment enabled by default");
    SetEnvironmentVariableA(names[2],"1");
    require(engine_start(nullptr)&&nativeUploadOptIn&&nativeUploadOuterOptIn,"outer reuse did not enable upload hook prerequisite");
    SetEnvironmentVariableA(names[3],"1");
    require(engine_start(nullptr)&&nativeUploadBatchOptIn&&!nativeUploadOptIn&&!nativeUploadOuterOptIn,"batch mode did not supersede outer reuse");
    SetEnvironmentVariableA(names[3],nullptr);SetEnvironmentVariableA(names[2],"01");
    require(engine_start(nullptr)&&!nativeUploadOptIn&&!nativeUploadOuterOptIn,"malformed CPU opt-in accepted");
    SetEnvironmentVariableA(names[2],"1");engine_start(nullptr);engine_stop();
    require(!nativeUploadOptIn&&!nativeUploadOuterOptIn,"CPU outer reuse opt-in survived teardown");
    for(size_t i=0;i<prior.size();++i)SetEnvironmentVariableA(names[i],prior[i].empty()?nullptr:prior[i].c_str());
}

// 0.3.3: hook targets must be plausible function starts in the real PE image.
void hookTargetTest(){
    using namespace lmuqv;
    const auto saved=base;base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    DWORD64 imageBase=0;
    const auto* entry=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&outerUploadSettingsTest),&imageBase,nullptr);
    require(entry&&imageBase==base,"framed test function has no unwind entry");
    require(hookTargetPlausible(entry->BeginAddress),"framed function start rejected");
    require(entry->EndAddress-entry->BeginAddress>1&&!hookTargetPlausible(entry->BeginAddress+1),"target inside a framed function accepted");
    require(!hookTargetPlausible(0),"PE header accepted as hook target");
    require(!hookTargetPlausible(reinterpret_cast<std::uintptr_t>(&selectedHooks)-base),"data section accepted as hook target");
    base=0;require(!hookTargetPlausible(0x1000),"missing image accepted");
    // Synthetic mapped image: arbitrary gaps are rejected, only complete known
    // leaf bodies can be used without an unwind record.
    std::vector<unsigned char> image(0x600000);
    base=reinterpret_cast<std::uintptr_t>(image.data());
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+0x80);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;nt->FileHeader.NumberOfSections=1;nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt->OptionalHeader.SizeOfImage=static_cast<DWORD>(image.size());nt->OptionalHeader.NumberOfRvaAndSizes=16;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION]={0x800,sizeof(RUNTIME_FUNCTION)};
    auto* section=IMAGE_FIRST_SECTION(nt);section->VirtualAddress=0x1000;section->Misc.VirtualSize=0x5ff000;section->Characteristics=IMAGE_SCN_MEM_EXECUTE;
    auto* function=reinterpret_cast<RUNTIME_FUNCTION*>(base+0x800);function->BeginAddress=0x1000;function->EndAddress=0x1020;
    require(hookTargetPlausible(0x1000)&&!hookTargetPlausible(0x1001)&&!hookTargetPlausible(0x1030),"synthetic framed function/gap classification failed");
    std::memcpy(image.data()+0x557810,projection_leaf_anchor.data(),projection_leaf_anchor.size());
    std::memcpy(image.data()+fovScaleRva,fovScaleAnchor.data(),fovScaleAnchor.size());
    require(hookTargetPlausible(0x557810)&&hookTargetPlausible(fovScaleRva),"verified leaf rejected");
    require(!hookTargetPlausible(0x557811)&&!hookTargetPlausible(fovScaleRva+1),"leaf interior accepted");
    image[0x557841]=0x90;image[fovScaleRva+7]=0x90;
    require(!hookTargetPlausible(0x557810)&&!hookTargetPlausible(fovScaleRva),"partial leaf signatures accepted");
    // A familiar classifier prologue/call site must not admit an interior
    // address after a game update. Exercise the policy used to install the hook.
    const unsigned char classifyCall[]{0xe8,0xec,0x3d,0x11,0x00,0x0f,0xba,0xe0,0x11};
    std::memcpy(image.data()+nativeClassifyRva,nativeClassifyAnchor.data(),nativeClassifyAnchor.size());
    std::memcpy(image.data()+0x41c8af,classifyCall,sizeof(classifyCall));
    function->BeginAddress=static_cast<DWORD>(nativeClassifyRva);
    function->EndAddress=function->BeginAddress+0x100;
    require(nativeClassifyTargetVerified(),"verified classifier target rejected");
    --function->BeginAddress;
    require(!nativeClassifyTargetVerified(),"classifier anchor inside another function accepted");
    ++function->BeginAddress;
    image[0x41c8af]^=1;
    require(!nativeClassifyTargetVerified(),"classifier hook accepted a changed call site");
    image[0x41c8af]^=1;image[nativeClassifyRva]^=1;
    require(!nativeClassifyTargetVerified(),"classifier hook accepted a changed prologue");
    image[nativeClassifyRva]^=1;
    section->Characteristics=IMAGE_SCN_MEM_READ;
    require(!nativeClassifyTargetVerified(),"classifier hook accepted a non-executable section");
    section->Characteristics=IMAGE_SCN_MEM_EXECUTE;
    const unsigned char transformCall[]{0xe8,0x91,0x27,0x00,0x00,0xf6,0xc3,0x08,0x74,0x27};
    std::memcpy(image.data()+nativeTransformRva,nativeTransformAnchor.data(),nativeTransformAnchor.size());
    std::memcpy(image.data()+0x562aca,transformCall,sizeof(transformCall));
    function->BeginAddress=static_cast<DWORD>(nativeTransformRva);function->EndAddress=function->BeginAddress+0x800;
    require(nativeTransformTargetVerified(),"verified transform helper target rejected");
    --function->BeginAddress;
    require(!nativeTransformTargetVerified(),"transform helper inside another function accepted");
    ++function->BeginAddress;image[0x562aca]^=1;
    require(!nativeTransformTargetVerified(),"transform helper accepted a changed caller");
    image[0x562aca]^=1;image[nativeTransformRva]^=1;
    require(!nativeTransformTargetVerified(),"transform helper accepted a changed prologue");
    image[nativeTransformRva]^=1;section->Characteristics=IMAGE_SCN_MEM_READ;
    require(!nativeTransformTargetVerified(),"transform helper accepted a non-executable section");
    section->Characteristics=IMAGE_SCN_MEM_EXECUTE;
    const unsigned char ownerCall0[]{0xe8,0x20,0x22,0x00,0x00},ownerCall1[]{0xe8,0x8e,0x20,0x00,0x00};
    const unsigned char ownerCall2[]{0xe8,0x14,0x13,0x00,0x00},ownerCall3[]{0xe8,0x55,0x11,0x00,0x00};
    const unsigned char ownerMasks[]{0x44,0x8b,0x41,0x74,0x45,0x85,0xc0,0x74,0x2c,0x48,0x85,0xd2,0x74,0x17,0x8b,0x82,0x0c,0x01,0x00,0x00};
    const unsigned char sourceCall[]{0xe8,0x0a,0xfd,0xff,0xff};
    std::memcpy(image.data()+nativeOwnerRva,nativeOwnerAnchor.data(),nativeOwnerAnchor.size());
    std::memcpy(image.data()+0x5def1b,ownerCall0,sizeof(ownerCall0));std::memcpy(image.data()+0x5df0ad,ownerCall1,sizeof(ownerCall1));
    std::memcpy(image.data()+0x5dfe27,ownerCall2,sizeof(ownerCall2));std::memcpy(image.data()+0x5dffe6,ownerCall3,sizeof(ownerCall3));
    std::memcpy(image.data()+0x41a9a0,ownerMasks,sizeof(ownerMasks));std::memcpy(image.data()+0x5e1201,sourceCall,sizeof(sourceCall));
    function->BeginAddress=static_cast<DWORD>(nativeOwnerRva);function->EndAddress=function->BeginAddress+0x13b;
    require(nativeOwnerTargetVerified(),"verified owner traversal target rejected");
    --function->BeginAddress;require(!nativeOwnerTargetVerified(),"owner traversal interior accepted");++function->BeginAddress;
    for(const auto rva:{uintptr_t(0x5def1b),uintptr_t(0x5df0ad),uintptr_t(0x5dfe27),uintptr_t(0x5dffe6),uintptr_t(0x41a9a0),uintptr_t(0x5e1201)}){
        image[rva]^=1;require(!nativeOwnerTargetVerified(),"owner traversal accepted changed source/caller layout");image[rva]^=1;
    }
    section->Characteristics=IMAGE_SCN_MEM_READ;require(!nativeOwnerTargetVerified(),"owner traversal accepted non-executable target");
    section->Characteristics=IMAGE_SCN_MEM_EXECUTE;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size=0xffffffff;
    require(!hookTargetPlausible(0x1000),"out-of-image exception directory accepted");
    base=saved;
}

}
int main(int argc,char** argv) {
    {
        using namespace lmuqv;
        // A bad right-eye comparison must not poison later native camera
        // pairs. Only the next verified left boundary can clear that refusal.
        CONTEXT cameraCaller{};cameraCaller.R13=1;cameraCaller.R14=77;cameraCaller.R15=88;
        for(unsigned failure=0;failure<5;++failure){
            spsNativePoseRejected=spsNativeViewRejected=true;auto wrong=cameraCaller;
            if(failure==0)wrong.R12=1;else if(failure==1)wrong.R13=0;
            else if(failure==2)wrong.R14=78;else if(failure==3)wrong.R15=89;
            require(!beginNativeCameraPair(wrong,failure==4?0:77,88),"unverified camera boundary cleared a pair refusal");
            require(spsNativePoseRejected&&spsNativeViewRejected,"invalid camera input discarded refusal state");
        }
        require(beginNativeCameraPair(cameraCaller,77,88),"fresh left camera pair could not recover");
        require(!spsNativePoseRejected&&!spsNativeViewRejected,"old right-eye refusal leaked into fresh camera pair");
    }
    using namespace lmuqv;
    {const auto saved=_mm_getcsr();const bool preparation=spsPreparationOptIn;spsPreparationOptIn=false;
        nativeCameraStateOriginal=cameraStateForward;nativeCameraPoseOriginal=cameraPoseForward;
        nativeCameraStateHook(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2));
        assert(cameraStateForwards==1&&_mm_getcsr()==0x1fbf);
        float values[6]{};std::memcpy(values,cameraPoseBits,sizeof(values));
        nativeCameraPoseHook(reinterpret_cast<void*>(3),values[0],values[1],values[2],values[3],values[4],values[5]);
        assert(cameraPoseForwards==1&&_mm_getcsr()==0x1fbf);
        spsPreparationOptIn=preparation;_mm_setcsr(saved);}
    {const unsigned saved=_mm_getcsr();uint32_t bits=0x7fc12345u;float bias{};std::memcpy(&bias,&bits,sizeof(bias));
        nativeOwnerOriginal=ownerForward;
        for(bool enabled:{false,true}){nativeOwnerInputEnabled.store(enabled);_mm_setcsr(0x1f80);
            nativeOwnerHook(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),bias);
            require(_mm_getcsr()==0x1fbf,"owner forwarding changed native FP state");}
        require(ownerForwardCalls==2,"owner hook did not forward original exactly once");nativeOwnerInputEnabled.store(false);_mm_setcsr(saved);}
    if(argc==2&&std::strcmp(argv[1],"--process-identity")==0) {
        wchar_t path[32768]{};require(GetModuleFileNameW(nullptr,path,32768)!=0,"Process path unavailable");
        const bool namedLmu=_wcsicmp(std::filesystem::path(path).filename().c_str(),L"Le Mans Ultimate.exe")==0;
        require(correctProcess()==namedLmu,"Native executable identity must not require a fixed disk hash");
        return 0;
    }
    // QVFR issues opaque handles 1 and 2. Exact native identity, not numeric
    // value, decides whether an eye is assigned (reported log regression).
    XrSwapchain eyeHandles[]{reinterpret_cast<XrSwapchain>(1),XR_NULL_HANDLE};
    std::uintptr_t eyeVector[]{reinterpret_cast<std::uintptr_t>(eyeHandles),
        reinterpret_cast<std::uintptr_t>(eyeHandles+2)};
    const auto savedBase=base;
    base=reinterpret_cast<std::uintptr_t>(eyeVector)-0x19b28d0;
    installed=true;nativeRuntimeSettings=true;
    require(engine_game_eye(eyeHandles[0])==0,"valid QVFR handle 1 rejected for left eye");
    eyeHandles[1]=reinterpret_cast<XrSwapchain>(2);
    require(engine_game_eye(eyeHandles[1])==1,"right eye identity lost");
    require(engine_game_eye(XR_NULL_HANDLE)==2,"null handle accepted");
    require(engine_game_eye(reinterpret_cast<XrSwapchain>(3))==2,"auxiliary handle accepted");
    eyeHandles[1]=eyeHandles[0];
    require(engine_game_eye(eyeHandles[0])==2,"ambiguous native handles accepted");
    base=savedBase;installed=false;nativeRuntimeSettings=false;
    SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");
    uiPlaneSizeTest();
    hookTargetTest();
    outerUploadScopeTest();outerUploadSettingsTest();
    unsigned nativeWidth=0,nativeHeight=0;
    nativeRuntimeSettings=true;
    nativeOuterSizes=parse_output_sizes("10","200");
    require(nativeOuterDimensions(0,32,32,nativeWidth,nativeHeight)&&nativeWidth==24&&nativeHeight==16,"runtime left target used percentages");
    require(nativeOuterDimensions(1,32,32,nativeWidth,nativeHeight)&&nativeWidth==20&&nativeHeight==28,"runtime right target lost asymmetric dimensions");
    require(!nativeOuterDimensions(0,31,32,nativeWidth,nativeHeight),"LMU input scaling must reject mismatched native target size");
    nativeRuntimeSettings=false;
    require(nativeOuterDimensions(0,32,32,nativeWidth,nativeHeight)&&nativeWidth==8&&nativeHeight==8,"legacy native percentage sizing changed");
    nativeCropWidth=parse_crop_percent("75");nativeCropHeight=parse_crop_percent("50");
    require(nativeOuterDimensions(0,32,32,nativeWidth,nativeHeight)&&nativeWidth==6&&nativeHeight==4,"legacy bank must match cropped XR outer output");
    nativeCropWidth={};nativeCropHeight={};
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
