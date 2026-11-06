#pragma once
#include "stereo_com_ref.hpp"
#include "xr_frame_inputs.hpp"
#include <d3d11.h>
#include <array>
#include <cmath>
#include <vector>

namespace lmuqv {
struct SpsFrameTargetDescription {
    UINT width=0,height=0;
    DXGI_FORMAT color=DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT depth=DXGI_FORMAT_D32_FLOAT;
    UINT samples=1,quality=0;
};
struct SpsFrameClear {
    std::array<float,4> color{0,0,0,1};
    float depth=0;
    UINT8 stencil=0;
};
struct SpsFrameImage {
    ID3D11Texture2D* color=nullptr;
    ID3D11Texture2D* depth=nullptr;
    ID3D11ShaderResourceView* sampledColor=nullptr;
    UINT subresource=0,width=0,height=0;
    StereoFrameIdentity identity;
};

// The production renderer owns the complete initial contents of both eyes.
// No application color/depth bank, original-eye draw, or native seed is used.
// Allocation survives frame boundaries; frame identity controls access.
class SpsFrameTargets {
    enum class Phase {Empty,Prepared,Rendered,Complete,Failed};
    Phase phase_=Phase::Empty;
    StereoFrameIdentity identity_;
    StereoComRef<ID3D11Device> device_;
    StereoComRef<ID3D11DeviceContext> context_;
    SpsFrameTargetDescription description_;
    StereoComRef<ID3D11Texture2D> color_,depth_;
    std::array<StereoComRef<ID3D11RenderTargetView>,3> colorViews_;
    std::array<StereoComRef<ID3D11DepthStencilView>,3> depthViews_;
    std::array<StereoComRef<ID3D11ShaderResourceView>,2> sampledColor_;
    unsigned consumed_=0;
    uint64_t allocations_=0;
    const char* reason_="SPS targets not allocated";
    static bool same(const SpsFrameTargetDescription& a,const SpsFrameTargetDescription& b){
        return a.width==b.width&&a.height==b.height&&a.color==b.color&&a.depth==b.depth&&a.samples==b.samples&&a.quality==b.quality;
    }
    static DXGI_FORMAT depth_resource(DXGI_FORMAT f){
        if(f==DXGI_FORMAT_D32_FLOAT)return DXGI_FORMAT_R32_TYPELESS;
        if(f==DXGI_FORMAT_D24_UNORM_S8_UINT)return DXGI_FORMAT_R24G8_TYPELESS;
        if(f==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)return DXGI_FORMAT_R32G8X24_TYPELESS;
        if(f==DXGI_FORMAT_D16_UNORM)return DXGI_FORMAT_R16_TYPELESS;
        return DXGI_FORMAT_UNKNOWN;
    }
    bool allocate(ID3D11Device* device,const SpsFrameTargetDescription& d){
        StereoComRef<ID3D11Texture2D> color,depth;
        std::array<StereoComRef<ID3D11RenderTargetView>,3> rt;
        std::array<StereoComRef<ID3D11DepthStencilView>,3> ds;
        std::array<StereoComRef<ID3D11ShaderResourceView>,2> sampled;
        D3D11_TEXTURE2D_DESC texture{};texture.Width=d.width;texture.Height=d.height;
        texture.MipLevels=1;texture.ArraySize=2;texture.Format=d.color;
        texture.SampleDesc={d.samples,d.quality};texture.Usage=D3D11_USAGE_DEFAULT;
        texture.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(device->CreateTexture2D(&texture,nullptr,color.put())))return false;
        texture.Format=depth_resource(d.depth);texture.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        if(FAILED(device->CreateTexture2D(&texture,nullptr,depth.put())))return false;
        for(unsigned i=0;i<3;++i){
            D3D11_RENDER_TARGET_VIEW_DESC r{};r.Format=d.color;
            D3D11_DEPTH_STENCIL_VIEW_DESC s{};s.Format=d.depth;
            if(d.samples==1){
                r.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                r.Texture2DArray.FirstArraySlice=i==2?0:i;r.Texture2DArray.ArraySize=i==2?2:1;
                s.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                s.Texture2DArray.FirstArraySlice=i==2?0:i;s.Texture2DArray.ArraySize=i==2?2:1;
            }else{
                r.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
                r.Texture2DMSArray.FirstArraySlice=i==2?0:i;r.Texture2DMSArray.ArraySize=i==2?2:1;
                s.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
                s.Texture2DMSArray.FirstArraySlice=i==2?0:i;s.Texture2DMSArray.ArraySize=i==2?2:1;
            }
            if(FAILED(device->CreateRenderTargetView(color.get(),&r,rt[i].put()))||
               FAILED(device->CreateDepthStencilView(depth.get(),&s,ds[i].put())))return false;
        }
        for(unsigned eye=0;eye<2;++eye){
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=d.color;
            if(d.samples==1){view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                view.Texture2DArray.MipLevels=1;view.Texture2DArray.FirstArraySlice=eye;view.Texture2DArray.ArraySize=1;}
            else{view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
                view.Texture2DMSArray.FirstArraySlice=eye;view.Texture2DMSArray.ArraySize=1;}
            if(FAILED(device->CreateShaderResourceView(color.get(),&view,sampled[eye].put())))return false;
        }
        color_=std::move(color);depth_=std::move(depth);colorViews_=std::move(rt);depthViews_=std::move(ds);
        sampledColor_=std::move(sampled);device_=StereoComRef<ID3D11Device>(device);description_=d;++allocations_;return true;
    }
public:
    bool begin(ID3D11DeviceContext* context,const StereoFrameIdentity& key,const SpsFrameTargetDescription& d,const SpsFrameClear& clear={}){
        const auto fail=[&](const char* why){reason_=why;phase_=Phase::Failed;return false;};
        if(identity_.valid()&&identity_.thread!=GetCurrentThreadId())return false;
        if(!context||!key.valid()||key.thread!=GetCurrentThreadId()||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
            return fail("invalid SPS target owner/frame");
        if(phase_==Phase::Prepared||phase_==Phase::Rendered)return false;
        if(identity_==key)return fail("SPS frame targets already used for this identity");
        if(!d.width||!d.height||d.width>16384||d.height>16384||!depth_resource(d.depth)||
           (d.samples!=1&&d.samples!=2&&d.samples!=4&&d.samples!=8)||clear.depth<0||clear.depth>1||!std::isfinite(clear.depth))
            return fail("unsupported SPS target description");
        for(float v:clear.color)if(!std::isfinite(v))return fail("non-finite SPS clear color");
        StereoComRef<ID3D11Device> device;context->GetDevice(device.put());
        if(!device||FAILED(device->GetDeviceRemovedReason()))return fail("SPS target device unavailable");
        if(device_.get()!=device.get()||!same(description_,d)){
            UINT support=0,qualities=0;
            if(FAILED(device->CheckFormatSupport(d.color,&support))||
               (support&(D3D11_FORMAT_SUPPORT_RENDER_TARGET|D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))!=(D3D11_FORMAT_SUPPORT_RENDER_TARGET|D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)||
               FAILED(device->CheckMultisampleQualityLevels(d.color,d.samples,&qualities))||d.quality>=qualities||
               FAILED(device->CheckMultisampleQualityLevels(d.depth,d.samples,&qualities))||d.quality>=qualities)
                return fail("SPS target format/sample support unavailable");
            if(!allocate(device.get(),d))return fail("SPS target allocation failed");
        }
        context_=StereoComRef<ID3D11DeviceContext>(context);identity_=key;consumed_=0;
        context->ClearRenderTargetView(colorViews_[2].get(),clear.color.data());
        const bool stencil=d.depth==DXGI_FORMAT_D24_UNORM_S8_UINT||d.depth==DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        context->ClearDepthStencilView(depthViews_[2].get(),D3D11_CLEAR_DEPTH|(stencil?D3D11_CLEAR_STENCIL:0),clear.depth,clear.stencil);
        phase_=Phase::Prepared;reason_="SPS-owned eye targets cleared";return true;
    }
    bool matches(ID3D11DeviceContext* context,const StereoFrameIdentity& key)const{
        return context==context_.get()&&key==identity_&&key.thread==GetCurrentThreadId()&&
            (phase_==Phase::Prepared||phase_==Phase::Rendered);
    }
    template<class Input>bool accepts_packets(const Input& packets,const StereoFrameIdentity& key)const{
        if(phase_!=Phase::Prepared||key!=identity_||key.thread!=GetCurrentThreadId())return false;
        for(const auto& eye:packets)for(const auto& d:eye){
            if(d.viewport.TopLeftX<0||d.viewport.TopLeftY<0||d.viewport.Width<=0||d.viewport.Height<=0||
               d.viewport.TopLeftX+d.viewport.Width>description_.width||d.viewport.TopLeftY+d.viewport.Height>description_.height)return false;
            if(d.depth){D3D11_DEPTH_STENCIL_DESC state{};d.depth->GetDesc(&state);
                if(state.StencilEnable&&description_.depth!=DXGI_FORMAT_D24_UNORM_S8_UINT&&description_.depth!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT)return false;}
        }
        return true;
    }
    ID3D11RenderTargetView* color_target(unsigned eye)const{return eye<3?colorViews_[eye].get():nullptr;}
    ID3D11DepthStencilView* depth_target(unsigned eye)const{return eye<3?depthViews_[eye].get():nullptr;}
    void mark_rendered(const StereoFrameIdentity& key){if(key==identity_&&key.thread==GetCurrentThreadId()&&phase_==Phase::Prepared)phase_=Phase::Rendered;}
    bool image(unsigned eye,const StereoFrameIdentity& key,SpsFrameImage& out)const{
        out={};if(eye>1||key!=identity_||key.thread!=GetCurrentThreadId()||phase_!=Phase::Rendered||(consumed_&(1u<<eye)))return false;
        out={color_.get(),depth_.get(),sampledColor_[eye].get(),eye,description_.width,description_.height,key};return true;
    }
    bool consumed(unsigned eye,const StereoFrameIdentity& key){
        SpsFrameImage image;if(!this->image(eye,key,image))return false;consumed_|=1u<<eye;if(consumed_==3)phase_=Phase::Complete;return true;
    }
    bool complete(const StereoFrameIdentity& key)const{return key==identity_&&key.thread==GetCurrentThreadId()&&phase_==Phase::Complete;}
    void abort(){if(!identity_.valid()||identity_.thread==GetCurrentThreadId()){phase_=Phase::Failed;reason_="SPS frame aborted";}}
    uint64_t allocations()const{return allocations_;}
    const char* reason()const{return reason_;}
};
}
