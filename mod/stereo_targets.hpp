#pragma once
#include "stereo_gpu_resources.hpp"

namespace lmuqv {
struct StereoEyeTargetInput {
    StereoResourceLease colorSeed,depthSeed;
    UINT colorSubresource=0,depthSubresource=0;
    StereoComRef<ID3D11RenderTargetView> colorDestination;
    StereoComRef<ID3D11DepthStencilView> depthDestination;
};
class StereoTargetBridge {
    enum class Phase {Empty,Ready,Rendered,LeftConsumed,Complete,Aborted};
    struct Destination {
        StereoComRef<ID3D11Texture2D> color,depth;
        UINT colorSubresource=0,depthSubresource=0;
    };
    Phase phase_=Phase::Empty;
    StereoFrameIdentity identity_;
    StereoComRef<ID3D11DeviceContext> context_;
    StereoComRef<ID3D11Texture2D> color_,depth_;
    std::array<StereoComRef<ID3D11RenderTargetView>,3> colorViews_;
    std::array<StereoComRef<ID3D11DepthStencilView>,3> depthViews_;
    std::array<Destination,2> destinations_;
    const char* reason_="no target scope";
    UINT width_=0,height_=0,samples_=0;
    static bool color_destination(ID3D11RenderTargetView* view,Destination& out,D3D11_TEXTURE2D_DESC& texture,DXGI_FORMAT& format){
        if(!view)return false;D3D11_RENDER_TARGET_VIEW_DESC d{};view->GetDesc(&d);format=d.Format;UINT slice=0,mip=0;
        switch(d.ViewDimension){
        case D3D11_RTV_DIMENSION_TEXTURE2D:mip=d.Texture2D.MipSlice;break;
        case D3D11_RTV_DIMENSION_TEXTURE2DMS:break;
        case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:if(d.Texture2DArray.ArraySize!=1)return false;slice=d.Texture2DArray.FirstArraySlice;mip=d.Texture2DArray.MipSlice;break;
        case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:if(d.Texture2DMSArray.ArraySize!=1)return false;slice=d.Texture2DMSArray.FirstArraySlice;break;
        default:return false;
        }
        StereoComRef<ID3D11Resource> resource;view->GetResource(resource.put());
        if(FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(out.color.put()))))return false;
        out.color->GetDesc(&texture);if(mip||slice>=texture.ArraySize||texture.Usage!=D3D11_USAGE_DEFAULT||texture.CPUAccessFlags)return false;
        out.colorSubresource=D3D11CalcSubresource(mip,slice,texture.MipLevels);return true;
    }
    static bool depth_destination(ID3D11DepthStencilView* view,Destination& out,D3D11_TEXTURE2D_DESC& texture,DXGI_FORMAT& format,UINT& flags){
        if(!view)return false;D3D11_DEPTH_STENCIL_VIEW_DESC d{};view->GetDesc(&d);format=d.Format;flags=d.Flags;UINT slice=0,mip=0;
        switch(d.ViewDimension){
        case D3D11_DSV_DIMENSION_TEXTURE2D:mip=d.Texture2D.MipSlice;break;
        case D3D11_DSV_DIMENSION_TEXTURE2DMS:break;
        case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:if(d.Texture2DArray.ArraySize!=1)return false;slice=d.Texture2DArray.FirstArraySlice;mip=d.Texture2DArray.MipSlice;break;
        case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:if(d.Texture2DMSArray.ArraySize!=1)return false;slice=d.Texture2DMSArray.FirstArraySlice;break;
        default:return false;
        }
        StereoComRef<ID3D11Resource> resource;view->GetResource(resource.put());
        if(FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(out.depth.put()))))return false;
        out.depth->GetDesc(&texture);if(mip||slice>=texture.ArraySize||texture.Usage!=D3D11_USAGE_DEFAULT||texture.CPUAccessFlags)return false;
        out.depthSubresource=D3D11CalcSubresource(mip,slice,texture.MipLevels);return true;
    }
    static bool same_surface(const D3D11_TEXTURE2D_DESC& a,const D3D11_TEXTURE2D_DESC& b){
        return a.Width==b.Width&&a.Height==b.Height&&a.Format==b.Format&&a.SampleDesc.Count==b.SampleDesc.Count&&a.SampleDesc.Quality==b.SampleDesc.Quality;
    }
public:
    StereoTargetBridge()=default;
    StereoTargetBridge(const StereoTargetBridge&)=delete;
    StereoTargetBridge& operator=(const StereoTargetBridge&)=delete;
    template<class Packets>bool accepts_packets(const Packets&,const StereoFrameIdentity& key)const{
        return phase_==Phase::Ready&&identity_==key&&key.thread==GetCurrentThreadId();
    }
    // Seeds are independent owned snapshots taken at each eye's required native
    // initial-state point. Two aliases of one reused native bank are not seeds.
    bool prepare(ID3D11DeviceContext* context,const StereoFrameIdentity& key,
        const std::array<StereoEyeTargetInput,2>& input,uint64_t byteBudget=512ull*1024*1024) noexcept {
        if(context_&&identity_.thread!=GetCurrentThreadId()){reason_="foreign target thread";return false;}
        if(phase_==Phase::Ready||phase_==Phase::Rendered||phase_==Phase::LeftConsumed){reason_="target scope still in flight";return false;}
        phase_=Phase::Empty;identity_={};context_.reset();color_.reset();depth_.reset();
        colorViews_={};depthViews_={};destinations_={};width_=height_=samples_=0;
        const auto fail=[&](const char* reason){reason_=reason;return false;};
        if(!context||!key.valid()||key.thread!=GetCurrentThreadId()||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return fail("invalid target scope");
        std::array<StereoComRef<ID3D11Texture2D>,2> colorSeeds,depthSeeds;
        std::array<D3D11_TEXTURE2D_DESC,2> colorDesc{},depthDesc{};
        DXGI_FORMAT colorFormat[2]{},depthFormat[2]{};UINT depthFlags[2]{};
        StereoComRef<ID3D11Device> device;context->GetDevice(device.put());
        if(device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0||FAILED(device->GetDeviceRemovedReason()))return fail("target device unavailable");
        for(unsigned eye=0;eye<2;++eye){const auto& s=input[eye];
            if(!s.colorSeed||!s.depthSeed||s.colorSeed.identity()!=key||s.depthSeed.identity()!=key)return fail("missing/stale per-eye target seeds");
            if(!color_destination(s.colorDestination.get(),destinations_[eye],colorDesc[eye],colorFormat[eye])||
               !depth_destination(s.depthDestination.get(),destinations_[eye],depthDesc[eye],depthFormat[eye],depthFlags[eye]))return fail("unsupported native target view");
            if(FAILED(s.colorSeed.resource()->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(colorSeeds[eye].put())))||
               FAILED(s.depthSeed.resource()->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(depthSeeds[eye].put()))))return fail("2D target seeds required");
            D3D11_TEXTURE2D_DESC c{},d{};colorSeeds[eye]->GetDesc(&c);depthSeeds[eye]->GetDesc(&d);
            if(!same_surface(c,colorDesc[eye])||!same_surface(d,depthDesc[eye])||
               s.colorSubresource>=c.MipLevels*c.ArraySize||s.depthSubresource>=d.MipLevels*d.ArraySize||
               s.colorSubresource%c.MipLevels||s.depthSubresource%d.MipLevels)return fail("target seed shape/subresource mismatch");
            StereoComRef<ID3D11Device> a,b;destinations_[eye].color->GetDevice(a.put());destinations_[eye].depth->GetDevice(b.put());
            if(a.get()!=device.get()||b.get()!=device.get())return fail("foreign target device");
            colorSeeds[eye]->GetDevice(a.put());depthSeeds[eye]->GetDevice(b.put());
            if(a.get()!=device.get()||b.get()!=device.get())return fail("foreign target seed device");
        }
        if((input[0].colorSeed.same_contents(input[1].colorSeed)&&input[0].colorSubresource==input[1].colorSubresource)||
           (input[0].depthSeed.same_contents(input[1].depthSeed)&&input[0].depthSubresource==input[1].depthSubresource))return fail("aliased opposite-eye initial state");
        if(!same_surface(colorDesc[0],colorDesc[1])||!same_surface(depthDesc[0],depthDesc[1])||colorFormat[0]!=colorFormat[1]||depthFormat[0]!=depthFormat[1]||depthFlags[0]!=depthFlags[1]||
           colorDesc[0].Width!=depthDesc[0].Width||colorDesc[0].Height!=depthDesc[0].Height||colorDesc[0].SampleDesc.Count!=depthDesc[0].SampleDesc.Count||colorDesc[0].SampleDesc.Quality!=depthDesc[0].SampleDesc.Quality)
            return fail("incompatible eye/color/depth targets");
        auto c=colorDesc[0],d=depthDesc[0];c.MipLevels=d.MipLevels=1;c.ArraySize=d.ArraySize=2;c.Usage=d.Usage=D3D11_USAGE_DEFAULT;c.CPUAccessFlags=d.CPUAccessFlags=c.MiscFlags=d.MiscFlags=0;
        c.BindFlags=D3D11_BIND_RENDER_TARGET;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;uint64_t cb=0,db=0;
        if(!stereo_gpu_detail::texture_bytes(c,cb)||!stereo_gpu_detail::texture_bytes(d,db)||cb>byteBudget||db>byteBudget-cb)return fail("stereo target byte budget");
        if(FAILED(device->CreateTexture2D(&c,nullptr,color_.put()))||FAILED(device->CreateTexture2D(&d,nullptr,depth_.put())))return fail("stereo array target allocation failed");
        for(unsigned i=0;i<3;++i){const UINT first=i<2?i:0,count=i<2?1:2;
            D3D11_RENDER_TARGET_VIEW_DESC cv{};cv.Format=colorFormat[0];cv.ViewDimension=c.SampleDesc.Count>1?D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            if(c.SampleDesc.Count>1){cv.Texture2DMSArray.FirstArraySlice=first;cv.Texture2DMSArray.ArraySize=count;}else{cv.Texture2DArray.FirstArraySlice=first;cv.Texture2DArray.ArraySize=count;}
            D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=depthFormat[0];dv.Flags=depthFlags[0];dv.ViewDimension=d.SampleDesc.Count>1?D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
            if(d.SampleDesc.Count>1){dv.Texture2DMSArray.FirstArraySlice=first;dv.Texture2DMSArray.ArraySize=count;}else{dv.Texture2DArray.FirstArraySlice=first;dv.Texture2DArray.ArraySize=count;}
            if(FAILED(device->CreateRenderTargetView(color_.get(),&cv,colorViews_[i].put()))||FAILED(device->CreateDepthStencilView(depth_.get(),&dv,depthViews_[i].put())))return fail("stereo array view creation failed");
        }
        // All allocations and descriptor checks finish before queuing any copy.
        for(unsigned eye=0;eye<2;++eye){context->CopySubresourceRegion(color_.get(),eye,0,0,0,colorSeeds[eye].get(),input[eye].colorSubresource,nullptr);
            context->CopySubresourceRegion(depth_.get(),eye,0,0,0,depthSeeds[eye].get(),input[eye].depthSubresource,nullptr);}
        context_=StereoComRef<ID3D11DeviceContext>(context);identity_=key;width_=c.Width;height_=c.Height;samples_=c.SampleDesc.Count;
        phase_=Phase::Ready;reason_="independently seeded stereo arrays";return true;
    }
    ID3D11RenderTargetView* color_target(unsigned eye)const{return eye<3&&phase_!=Phase::Empty&&phase_!=Phase::Aborted?colorViews_[eye].get():nullptr;}
    ID3D11DepthStencilView* depth_target(unsigned eye)const{return eye<3&&phase_!=Phase::Empty&&phase_!=Phase::Aborted?depthViews_[eye].get():nullptr;}
    bool matches(ID3D11DeviceContext* context,const StereoFrameIdentity& key)const{return context_.get()==context&&key==identity_&&key.thread==GetCurrentThreadId()&&phase_==Phase::Ready;}
    bool mark_rendered(const StereoFrameIdentity& key){if(phase_!=Phase::Ready||key!=identity_||key.thread!=GetCurrentThreadId())return false;phase_=Phase::Rendered;return true;}
    // Call at that eye's opaque-output boundary, BEFORE downstream consumers.
    // Later native bank reuse cannot overwrite the other private array slice.
    bool consume(unsigned eye,const StereoFrameIdentity& key){
        if(key!=identity_||key.thread!=GetCurrentThreadId()||eye>1||
           (eye==0?phase_!=Phase::Rendered:phase_!=Phase::LeftConsumed))return false;
        context_->CopySubresourceRegion(destinations_[eye].color.get(),destinations_[eye].colorSubresource,0,0,0,color_.get(),eye,nullptr);
        context_->CopySubresourceRegion(destinations_[eye].depth.get(),destinations_[eye].depthSubresource,0,0,0,depth_.get(),eye,nullptr);
        phase_=eye?Phase::Complete:Phase::LeftConsumed;return true;
    }
    void abort(){if(!identity_.valid()||identity_.thread==GetCurrentThreadId())phase_=Phase::Aborted;}
    bool complete()const{return phase_==Phase::Complete;}
    const char* reason()const{return reason_;}
    UINT width()const{return width_;}UINT height()const{return height_;}UINT samples()const{return samples_;}
};
}
