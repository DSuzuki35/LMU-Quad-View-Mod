// Execute the production XR bridge and EndFrame route with only an SPS scene.
// Native engine/render/preparation entry points are poisoned below.
#define LMU_SPS_XR_TEST_HELPERS_ONLY
#include "sps_xr_output_test.cpp"
#include "../xr_layer.cpp"

namespace lmuqv {
bool engine_start(ID3D11DeviceContext*){assert(false);return false;}
void engine_stop(){assert(false);}
unsigned engine_game_eye(XrSwapchain){assert(false);return 2;}
bool native_sps_prepare(bool,void(*)(const char*)){assert(false);return false;}
void native_sps_stop(){assert(false);}
bool stereo_audit_prepare(bool,const wchar_t*,void(*)(const char*)){assert(false);return false;}
void stereo_audit_stop(){assert(false);}
void stereo_audit_frame_end(){assert(false);}
}
namespace {
unsigned endCalls=0,submitted=0,overlayCount=0;
std::array<XrCompositionLayerProjectionView,4> received;
XrResult XRAPI_CALL end(XrSession session,const XrFrameEndInfo* info){assert(session==s.session);++endCalls;submitted=overlayCount=0;
    for(unsigned i=0;i<info->layerCount;++i){const auto* layer=info->layers[i];
        if(layer->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION){++overlayCount;continue;}
        const auto& projection=*reinterpret_cast<const XrCompositionLayerProjection*>(layer);assert(projection.viewCount==4&&projection.space==s.locateSpace);
        ++submitted;std::copy(projection.views,projection.views+4,received.begin());}
    return XR_SUCCESS;}
void frame(unsigned number){
    s.frame=s.shouldRender=s.located=true;s.frames=number;s.predictedTime=s.locateTime=400+number;
    for(unsigned view=0;view<4;++view){s.views[view]={XR_TYPE_VIEW};s.views[view].pose.orientation.w=1;
        s.views[view].pose.position.x=view%2?.03f:-.03f;s.views[view].fov={-.7f,.7f,.7f,-.7f};}
}
void layer_run(bool hardware){
    Device d(hardware);Runtime runtime(d);const auto dispatch=runtime.dispatch();next={};
    next.CreateSwapchain=dispatch.create;next.DestroySwapchain=dispatch.destroy;next.EnumerateSwapchainImages=dispatch.images;
    next.AcquireSwapchainImage=dispatch.acquire;next.WaitSwapchainImage=dispatch.wait;next.ReleaseSwapchainImage=dispatch.release;next.EndFrame=end;
    s=State{};s.quad=true;s.session=reinterpret_cast<XrSession>(1);s.inputSessionSerial=9;s.locateSpace=reinterpret_cast<XrSpace>(3);
    s.context=d.context.get();s.context->AddRef();s.device=d.device.get();s.device->AddRef();
    s.colorInfo.width=s.colorInfo.height=8;s.colorInfo.format=DXGI_FORMAT_R8G8B8A8_UNORM;
    s.runtimeSizesReady=true;for(unsigned eye=0;eye<2;++eye)s.runtimeOuterWidth[eye]=s.runtimeOuterHeight[eye]=8;
    SpsRendererDescription description;assert(xr_prepare_sps_output(description)&&xr_sps_output_active());
    XrCompositionLayerProjection native{XR_TYPE_COMPOSITION_LAYER_PROJECTION};native.viewCount=2;
    native.views=reinterpret_cast<const XrCompositionLayerProjectionView*>(uintptr_t(0xdeadbeef)); // Must never be read.
    XrCompositionLayerQuad overlay{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const XrCompositionLayerBaseHeader* inputLayers[]{reinterpret_cast<const XrCompositionLayerBaseHeader*>(&native),reinterpret_cast<const XrCompositionLayerBaseHeader*>(&overlay)};
    XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};info.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;info.layerCount=2;info.layers=inputLayers;
    frame(1);info.displayTime=s.predictedTime;assert(XR_SUCCEEDED(layerEndFrame(s.session,&info)));assert(submitted==0&&overlayCount==1);
    SpsRenderer renderer;SpsScene scene;SpsCameraPlacement placement;description.background={.25f,.5f,.75f,1};
    for(unsigned number=2;number<=3;++number){frame(number);QuadFrameInputs inputs;assert(xr_frame_inputs(inputs));
        assert(renderer.render(d.context.get(),inputs,placement,description,scene));assert(xr_publish_sps_frame(renderer));
        info.displayTime=s.predictedTime;
        if(number==3){std::thread application([&]{assert(XR_SUCCEEDED(layerEndFrame(s.session,&info)));});application.join();}
        else assert(XR_SUCCEEDED(layerEndFrame(s.session,&info)));
        assert(submitted==1&&overlayCount==1);
        for(unsigned view=0;view<4;++view){assert(received[view].pose.position.x==inputs.views[view].pose.position.x);
            assert(received[view].subImage.swapchain==reinterpret_cast<XrSwapchain>(uintptr_t(0x1000+view)));
            auto& chain=runtime.chains[view];const auto pixels=read(d,chain.images[chain.index].get(),0,4);
            assert(std::abs(int(pixels[0])-64)<=1&&std::abs(int(pixels[1])-128)<=1&&std::abs(int(pixels[2])-191)<=1&&pixels[3]==255);}
        assert(!xr_publish_sps_frame(renderer));}
    frame(4);info.displayTime=s.predictedTime;assert(XR_SUCCEEDED(layerEndFrame(s.session,&info)));assert(submitted==0&&overlayCount==1);
    clearSession();assert(!xr_sps_output_active()&&runtime.destroyed==4);
    std::cout<<"SPS-only production XR bridge: owned render/publish/consume/end-frame, native projections poisoned, no native fallback on incomplete frames "
        <<(hardware?"hardware":"WARP")<<" passed\n";
}
}
int main(){layer_run(false);if(std::getenv("LMU_QV_STEREO_HARDWARE_TEST"))layer_run(true);return 0;}
