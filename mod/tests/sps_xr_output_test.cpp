#define main sps_target_helper_main
#include "sps_frame_targets_test.cpp"
#undef main
#include "../sps_xr_output.hpp"
#include <thread>

namespace {
struct Runtime {
    struct Chain {std::array<StereoComRef<ID3D11Texture2D>,2> images;bool acquired=false,waited=false;unsigned index=0,acquires=0;};
    Device& device;std::array<Chain,4> chains;
    unsigned created=0,destroyed=0,releases=0,releaseAttempts=0;
    int timeout=-1,releaseFailure=-1,createFailure=-1;
    explicit Runtime(Device& d):device(d){}
    static Runtime* current;
    static unsigned slot(XrSwapchain chain){const auto index=unsigned(reinterpret_cast<uintptr_t>(chain)-0x1000);assert(index<4);return index;}
    static XrResult XRAPI_CALL create(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out){auto& r=*current;const auto view=r.created++;
        if(int(view)==r.createFailure){*out=XR_NULL_HANDLE;return XR_ERROR_RUNTIME_FAILURE;}assert(view<4&&info->arraySize==1&&info->sampleCount==1&&info->mipCount==1);
        assert(info->usageFlags&XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT);
        D3D11_TEXTURE2D_DESC d{};d.Width=info->width;d.Height=info->height;d.ArraySize=d.MipLevels=1;d.Format=DXGI_FORMAT(info->format);
        d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;d.Usage=D3D11_USAGE_DEFAULT;
        for(auto& image:r.chains[view].images){assert(SUCCEEDED(r.device.device->CreateTexture2D(&d,nullptr,image.put())));
            StereoComRef<ID3D11RenderTargetView> rt;assert(SUCCEEDED(r.device.device->CreateRenderTargetView(image.get(),nullptr,rt.put())));
            const float black[4]{0,0,0,1};r.device.context->ClearRenderTargetView(rt.get(),black);}
        *out=reinterpret_cast<XrSwapchain>(uintptr_t(0x1000+view));return XR_SUCCESS;}
    static XrResult XRAPI_CALL destroy(XrSwapchain handle){auto& r=*current;r.chains[slot(handle)]=Chain{};++r.destroyed;return XR_SUCCESS;}
    static XrResult XRAPI_CALL images(XrSwapchain handle,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* images){*count=2;if(!capacity)return XR_SUCCESS;
        assert(capacity>=2&&images);auto* typed=reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
        for(unsigned i=0;i<2;++i){assert(typed[i].type==XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR);typed[i].texture=current->chains[slot(handle)].images[i].get();}return XR_SUCCESS;}
    static XrResult XRAPI_CALL acquire(XrSwapchain handle,const XrSwapchainImageAcquireInfo*,uint32_t* index){auto& chain=current->chains[slot(handle)];assert(!chain.acquired);
        chain.acquired=true;chain.waited=false;chain.index=chain.acquires++%2;*index=chain.index;return XR_SUCCESS;}
    static XrResult XRAPI_CALL wait(XrSwapchain handle,const XrSwapchainImageWaitInfo*){auto& r=*current;const auto view=slot(handle);auto& chain=r.chains[view];assert(chain.acquired&&!chain.waited);
        if(int(view)==r.timeout){r.timeout=-1;return XR_TIMEOUT_EXPIRED;}chain.waited=true;return XR_SUCCESS;}
    static XrResult XRAPI_CALL release(XrSwapchain handle,const XrSwapchainImageReleaseInfo*){auto& r=*current;const auto view=slot(handle);auto& chain=r.chains[view];assert(chain.acquired&&chain.waited);++r.releaseAttempts;
        if(int(view)==r.releaseFailure){r.releaseFailure=-1;return XR_ERROR_RUNTIME_FAILURE;}chain.acquired=chain.waited=false;++r.releases;return XR_SUCCESS;}
    SpsXrDispatch dispatch(){current=this;return {create,destroy,images,acquire,wait,release};}
};
Runtime* Runtime::current=nullptr;
void output_run(bool hardware){
    Device d(hardware);Runtime runtime(d);SpsXrOutput output;SpsXrOutputDescription description;description.format=DXGI_FORMAT_R8G8B8A8_UNORM;
    for(unsigned view=0;view<4;++view)description.views[view]={8-view,8-view};
    assert(output.configure(runtime.dispatch(),reinterpret_cast<XrSession>(1),9,d.context.get(),description));assert(runtime.created==4);
    const std::array<std::array<float,4>,4> colors{{{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,1}}};
    std::array<SpsFrameTargets,2> targets;
    for(unsigned frame=1;frame<=2;++frame){QuadFrameInputs input;input.identity={9,frame,3,400+frame,GetCurrentThreadId()};
        std::array<SpsFrameImage,4> images;
        for(unsigned pair=0;pair<2;++pair){assert(targets[pair].begin(d.context.get(),input.identity,{8,8,description.format,DXGI_FORMAT_D32_FLOAT,1,0}));
            for(unsigned eye=0;eye<2;++eye)d.context->ClearRenderTargetView(targets[pair].color_target(eye),colors[pair*2+eye].data());
            targets[pair].mark_rendered(input.identity);}
        for(unsigned view=0;view<4;++view){assert(targets[view/2].image(view%2,input.identity,images[view]));
            input.views[view]={XR_TYPE_VIEW};input.views[view].pose.orientation.w=1;input.views[view].pose.position.x=float(view+1);
            input.views[view].fov={-.9f,.8f,.7f,-.6f};}
        if(frame==1){auto incomplete=images;incomplete[3]={};assert(!output.publish(input,incomplete));assert(runtime.chains[0].acquires==0);
            runtime.timeout=1;assert(!output.publish(input,images));assert(output.result()==XR_TIMEOUT_EXPIRED&&!output.layer(input.identity));
            auto stale=input;++stale.identity.frame;assert(!output.publish(stale,images));assert(runtime.chains[0].acquires==1&&runtime.chains[2].acquires==0);
            const auto untouched=read(d,runtime.chains[0].images[0].get(),0,4);assert(untouched[0]==0);}
        if(frame==2){runtime.releaseFailure=2;assert(!output.publish(input,images));assert(!output.layer(input.identity));
            assert(runtime.releases==6&&runtime.releaseAttempts==7);}
        assert(output.publish(input,images));assert(!output.publish(input,images));
        const auto* layer=output.layer(input.identity);assert(layer&&layer->viewCount==4&&layer->space==reinterpret_cast<XrSpace>(3));
        for(unsigned view=0;view<4;++view){const auto& v=layer->views[view];assert(v.pose.position.x==float(view+1)&&v.fov.angleLeft==-.9f);
            assert(v.subImage.swapchain==reinterpret_cast<XrSwapchain>(uintptr_t(0x1000+view))&&v.subImage.imageArrayIndex==0);
            assert(v.subImage.imageRect.extent.width==int32_t(8-view)&&v.subImage.imageRect.extent.height==int32_t(8-view));
            auto& chain=runtime.chains[view];assert(chain.acquires==frame&&!chain.acquired);
            const auto bytes=read(d,chain.images[chain.index].get(),0,4);for(size_t at=0;at<bytes.size()/4;++at)
                for(unsigned channel=0;channel<4;++channel)assert(bytes[at*4+channel]==uint8_t(colors[view][channel]*255));
            assert(targets[view/2].consumed(view%2,input.identity));}
        assert(output.retired(input.identity)&&!output.layer(input.identity)&&!output.publish(input,images));
    }
    // An abandoned frame must release its runtime images and permit the next
    // SPS frame; it must not latch the session off or submit stale contents.
    QuadFrameInputs abandoned;abandoned.identity={9,3,3,403,GetCurrentThreadId()};std::array<SpsFrameImage,4> abandonedImages;
    for(unsigned pair=0;pair<2;++pair){assert(targets[pair].begin(d.context.get(),abandoned.identity,{8,8,description.format,DXGI_FORMAT_D32_FLOAT,1,0}));
        targets[pair].mark_rendered(abandoned.identity);for(unsigned eye=0;eye<2;++eye)assert(targets[pair].image(eye,abandoned.identity,abandonedImages[pair*2+eye]));}
    runtime.timeout=1;assert(!output.publish(abandoned,abandonedImages));assert(output.discard_pending());assert(!output.layer(abandoned.identity));
    assert(!output.publish(abandoned,abandonedImages));for(auto& target:targets)target.abort();
    assert(runtime.releases==10&&runtime.releaseAttempts==11);
    std::thread foreign([&]{assert(!output.stop());});foreign.join();
    assert(output.stop()&&runtime.destroyed==4);
    Runtime partial(d);partial.createFailure=2;SpsXrOutput failed;assert(!failed.configure(partial.dispatch(),reinterpret_cast<XrSession>(1),10,d.context.get(),description));
    assert(failed.stop()&&partial.destroyed==2);
    std::cout<<"SPS-only XR output: independent four-view array copies, complete-quad publication, acquire/wait/release retry ownership, stale frames and teardown "
        <<(hardware?"hardware":"WARP")<<" passed\n";
}
}
#ifndef LMU_SPS_XR_TEST_HELPERS_ONLY
int main(){output_run(false);if(std::getenv("LMU_QV_STEREO_HARDWARE_TEST"))output_run(true);return 0;}
#endif
