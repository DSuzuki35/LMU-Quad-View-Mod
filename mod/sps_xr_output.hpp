#pragma once
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <windows.h>
#include <d3d11.h>
#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"
#include "sps_frame_targets.hpp"

namespace lmuqv {
struct SpsXrDispatch {
    PFN_xrCreateSwapchain create=nullptr;
    PFN_xrDestroySwapchain destroy=nullptr;
    PFN_xrEnumerateSwapchainImages images=nullptr;
    PFN_xrAcquireSwapchainImage acquire=nullptr;
    PFN_xrWaitSwapchainImage wait=nullptr;
    PFN_xrReleaseSwapchainImage release=nullptr;
    explicit operator bool()const{return create&&destroy&&images&&acquire&&wait&&release;}
};
struct SpsXrViewSize {uint32_t width=0,height=0;};
struct SpsXrOutputDescription {
    std::array<SpsXrViewSize,4> views;
    DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
};

// Own all four XR images. A projection becomes available only after the entire
// SPS quad has been copied and released. No application-eye swapchain, native
// seed, later native right render or native projection is consumed here.
class SpsXrOutput {
    struct Chain {
        XrSwapchain handle=XR_NULL_HANDLE;
        std::vector<XrSwapchainImageD3D11KHR> images;
        uint32_t index=0;
        bool acquired=false,waited=false;
    };
    SpsXrDispatch next_;
    XrSession session_=XR_NULL_HANDLE;
    uint64_t serial_=0;
    DWORD owner_=0;
    StereoComRef<ID3D11DeviceContext> context_;
    StereoComRef<ID3D11Device> device_;
    SpsXrOutputDescription description_;
    std::array<Chain,4> chains_;
    std::array<XrCompositionLayerProjectionView,4> views_{};
    XrCompositionLayerProjection projection_{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    StereoFrameIdentity pending_,ready_,retired_;
    QuadFrameInputs renderedFrame_;
    bool copied_=false;
    XrResult result_=XR_SUCCESS;
    const char* reason_="SPS XR output not configured";
    static DXGI_FORMAT family(DXGI_FORMAT f){
        if(f==DXGI_FORMAT_R8G8B8A8_TYPELESS||f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        if(f==DXGI_FORMAT_B8G8R8A8_TYPELESS||f==DXGI_FORMAT_B8G8R8A8_UNORM||f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        return f==DXGI_FORMAT_R16G16B16A16_FLOAT?f:DXGI_FORMAT_UNKNOWN;
    }
    bool fail(const char* reason,XrResult result=XR_ERROR_RUNTIME_FAILURE){reason_=reason;result_=result;return false;}
    bool destination_valid(ID3D11Texture2D* texture,unsigned view){
        if(!texture)return false;D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        StereoComRef<ID3D11Device> device;texture->GetDevice(device.put());
        return device.get()==device_.get()&&d.Width==description_.views[view].width&&d.Height==description_.views[view].height&&
            d.ArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1&&family(d.Format)==family(description_.format);
    }
    bool source_valid(const SpsFrameImage& image,unsigned view,const StereoFrameIdentity& key){
        if(image.identity!=key||!image.color||image.width<description_.views[view].width||image.height<description_.views[view].height)return false;
        D3D11_TEXTURE2D_DESC d{};image.color->GetDesc(&d);StereoComRef<ID3D11Device> device;image.color->GetDevice(device.put());
        return device.get()==device_.get()&&d.SampleDesc.Count==1&&d.MipLevels==1&&image.subresource<d.ArraySize&&
            d.Width>=image.width&&d.Height>=image.height&&family(d.Format)==family(description_.format);
    }
public:
    SpsXrOutput()=default;
    SpsXrOutput(const SpsXrOutput&)=delete;
    SpsXrOutput& operator=(const SpsXrOutput&)=delete;
    bool configure(SpsXrDispatch dispatch,XrSession session,uint64_t serial,ID3D11DeviceContext* context,const SpsXrOutputDescription& description){
        if(session_||(owner_&&owner_!=GetCurrentThreadId()))return false;
        if(!dispatch||session==XR_NULL_HANDLE||!serial||!context||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||!family(description.format))return fail("invalid SPS XR configuration");
        for(auto view:description.views)if(!view.width||!view.height||view.width>16384||view.height>16384)return fail("invalid SPS XR view size");
        next_=dispatch;session_=session;serial_=serial;owner_=GetCurrentThreadId();description_=description;
        context_=StereoComRef<ID3D11DeviceContext>(context);context_->GetDevice(device_.put());
        if(!device_)return fail("SPS XR device unavailable");
        for(unsigned view=0;view<4;++view){auto& chain=chains_[view];XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            create.format=description.format;create.sampleCount=1;create.width=description.views[view].width;create.height=description.views[view].height;
            create.faceCount=create.arraySize=create.mipCount=1;
            auto result=next_.create(session_,&create,&chain.handle);if(XR_FAILED(result)||chain.handle==XR_NULL_HANDLE)return fail("SPS XR swapchain creation failed",result);
            uint32_t count=0;result=next_.images(chain.handle,0,&count,nullptr);
            if(XR_FAILED(result)||!count||count>16)return fail("SPS XR image count invalid",result);
            chain.images.resize(count);for(auto& image:chain.images)image={XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
            uint32_t written=0;result=next_.images(chain.handle,count,&written,reinterpret_cast<XrSwapchainImageBaseHeader*>(chain.images.data()));
            if(XR_FAILED(result)||written!=count)return fail("SPS XR image enumeration failed",result);
            for(const auto& image:chain.images)if(!destination_valid(image.texture,view))return fail("SPS XR image description/device differs");
        }
        reason_="four owned SPS XR swapchains ready";result_=XR_SUCCESS;return true;
    }
    bool publish(const QuadFrameInputs& frame,const std::array<SpsFrameImage,4>& images){
        const auto& key=frame.identity;
        if(!session_||owner_!=GetCurrentThreadId()||!key.valid()||key.thread!=owner_||key.session!=serial_||
           (retired_.valid()&&key.frame<=retired_.frame)||ready_.valid())return false;
        for(unsigned view=0;view<4;++view)if(chains_[view].images.empty()||!source_valid(images[view],view,key))return fail("incomplete/stale SPS XR source quad");
        if(pending_.valid()&&pending_!=key&&!discard_pending())return false;
        if(!pending_.valid()){renderedFrame_=frame;for(auto& view:renderedFrame_.views)view.next=nullptr;}pending_=key;
        // Acquire/wait the complete quartet before any destination write. An
        // unsuccessful wait keeps its acquisition; only the same frame retries.
        if(!copied_)for(auto& chain:chains_){
            if(!chain.acquired){XrSwapchainImageAcquireInfo info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                const auto result=next_.acquire(chain.handle,&info,&chain.index);if(XR_FAILED(result))return fail("SPS XR acquire failed",result);
                chain.acquired=true;}
            if(chain.index>=chain.images.size())return fail("SPS XR acquired image index invalid");
            if(!chain.waited){XrSwapchainImageWaitInfo info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};info.timeout=XR_INFINITE_DURATION;
                const auto result=next_.wait(chain.handle,&info);if(result!=XR_SUCCESS)return fail("SPS XR image wait incomplete",result);chain.waited=true;}
        }
        if(!copied_){for(unsigned view=0;view<4;++view){const auto size=description_.views[view];D3D11_BOX box{0,0,0,size.width,size.height,1};
                auto& chain=chains_[view];context_->CopySubresourceRegion(chain.images[chain.index].texture,0,0,0,0,images[view].color,images[view].subresource,&box);}
            if(FAILED(device_->GetDeviceRemovedReason()))return fail("SPS XR copy device lost");copied_=true;}
        for(auto& chain:chains_)if(chain.acquired){XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            const auto result=next_.release(chain.handle,&info);if(XR_FAILED(result))return fail("SPS XR image release failed",result);
            chain.acquired=chain.waited=false;}
        for(unsigned view=0;view<4;++view){auto& v=views_[view];v={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};v.pose=renderedFrame_.views[view].pose;v.fov=renderedFrame_.views[view].fov;
            v.subImage.swapchain=chains_[view].handle;v.subImage.imageRect.extent={int32_t(description_.views[view].width),int32_t(description_.views[view].height)};}
        projection_={XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection_.space=reinterpret_cast<XrSpace>(uintptr_t(key.space));projection_.viewCount=4;projection_.views=views_.data();
        ready_=key;pending_={};copied_=false;reason_="complete SPS quad copied and released to XR";result_=XR_SUCCESS;return true;
    }
    const XrCompositionLayerProjection* layer(const StereoFrameIdentity& key)const{
        // Immutable after release. The XR layer serializes publication and
        // submission even when EndFrame comes from another application thread.
        return ready_.valid()&&key==ready_?&projection_:nullptr;
    }
    bool retired(const StereoFrameIdentity& key){
        if(!layer(key))return false;retired_=ready_;ready_={};reason_="SPS XR quad retired";return true;
    }
    // Abandon an unpublished frame without abandoning runtime ownership. A
    // timeout retains its acquisition for another cleanup attempt; fresh SPS
    // frames can resume after the outstanding images are waited and released.
    bool discard_pending(){
        if(!pending_.valid())return true;
        for(auto& chain:chains_)if(chain.acquired){
            if(!chain.waited){XrSwapchainImageWaitInfo info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};info.timeout=0;
                const auto result=next_.wait(chain.handle,&info);if(result!=XR_SUCCESS)return fail("SPS XR abandoned image wait incomplete",result);chain.waited=true;}
            XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};const auto result=next_.release(chain.handle,&info);
            if(XR_FAILED(result))return fail("SPS XR abandoned image release failed",result);chain.acquired=chain.waited=false;
        }
        retired_=pending_;pending_={};copied_=false;renderedFrame_={};reason_="unpublished SPS XR frame retired";return true;
    }
private:
    bool destroy_chains(){
        bool okay=true;
        for(auto& chain:chains_)if(chain.handle!=XR_NULL_HANDLE){const auto result=next_.destroy(chain.handle);
            if(XR_FAILED(result)){okay=false;result_=result;}else chain=Chain{};}
        if(!okay)return fail("SPS XR swapchain destruction incomplete",result_);
        session_=XR_NULL_HANDLE;serial_=0;owner_=0;context_.reset();device_.reset();pending_=ready_=retired_={};renderedFrame_={};copied_=false;
        projection_={XR_TYPE_COMPOSITION_LAYER_PROJECTION};reason_="SPS XR output stopped";return true;
    }
public:
    bool stop(){return (!owner_||owner_==GetCurrentThreadId())&&destroy_chains();}
    // Session teardown is externally serialized by the XR layer mutex and may
    // originate on the application thread. It records no D3D context commands.
    bool stop_session(){return destroy_chains();}
    XrResult result()const{return result_;}
    const char* reason()const{return reason_;}
};
}
