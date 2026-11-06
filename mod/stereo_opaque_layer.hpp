#pragma once
#include "stereo_draw_capture.hpp"

namespace lmuqv {
// For a contiguous opaque range with strict depth comparison, the nearest
// fragment rendered against clear depth can be tested against the real initial
// depth later. No coverage mask is necessary: clear 1 fails LESS against every
// legal native depth; clear 0 similarly fails GREATER. Blending, stencil, partial
// color writes, destination reads and non-strict depth comparisons do not obey
// this reduction and must remain native.
class StereoOpaqueLayerTargets {
    enum class Phase {Empty,Ready,Rendered,LeftConsumed,Complete,Aborted};
    Phase phase_=Phase::Empty;
    StereoFrameIdentity identity_;
    StereoComRef<ID3D11Device> device_;
    StereoComRef<ID3D11DeviceContext> context_;
    StereoComRef<ID3D11Texture2D> color_,depth_;
    StereoComRef<ID3D11Resource> templateColor_,templateDepth_;
    std::array<StereoComRef<ID3D11RenderTargetView>,3> colorViews_;
    std::array<StereoComRef<ID3D11DepthStencilView>,3> depthViews_;
    StereoComRef<ID3D11ShaderResourceView> colorRead_,depthRead_;
    StereoComRef<ID3D11VertexShader> compositeVs_;
    StereoComRef<ID3D11PixelShader> compositePs_;
    StereoComRef<ID3D11RasterizerState> raster_;
    std::array<StereoComRef<ID3D11DepthStencilState>,2> depthStates_;
    std::array<StereoComRef<ID3D11Buffer>,2> eyes_;
    std::vector<StereoComRef<ID3D11Resource>> readSources_;
    UINT width_=0,height_=0;
    DXGI_FORMAT colorFormat_=DXGI_FORMAT_UNKNOWN;
    D3D11_COMPARISON_FUNC comparison_=D3D11_COMPARISON_NEVER;
    bool approved_=false;
    const char* reason_="no opaque layer";
    static bool color_view(ID3D11RenderTargetView* view,StereoComRef<ID3D11Resource>& resource,
        D3D11_TEXTURE2D_DESC& desc,DXGI_FORMAT& format){
        if(!view)return false;D3D11_RENDER_TARGET_VIEW_DESC d{};view->GetDesc(&d);format=d.Format;
        if(d.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D){if(d.Texture2D.MipSlice)return false;}
        else if(d.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DARRAY){if(d.Texture2DArray.ArraySize!=1||d.Texture2DArray.MipSlice)return false;}
        else return false;
        view->GetResource(resource.put());StereoComRef<ID3D11Texture2D> texture;
        if(FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.put()))))return false;
        texture->GetDesc(&desc);return desc.SampleDesc.Count==1&&desc.SampleDesc.Quality==0&&desc.Usage==D3D11_USAGE_DEFAULT&&!desc.CPUAccessFlags;
    }
    static bool depth_view(ID3D11DepthStencilView* view,StereoComRef<ID3D11Resource>& resource,D3D11_TEXTURE2D_DESC& desc){
        if(!view)return false;D3D11_DEPTH_STENCIL_VIEW_DESC d{};view->GetDesc(&d);
        if(d.Format!=DXGI_FORMAT_D32_FLOAT||d.Flags)return false;
        if(d.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D){if(d.Texture2D.MipSlice)return false;}
        else if(d.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DARRAY){if(d.Texture2DArray.ArraySize!=1||d.Texture2DArray.MipSlice)return false;}
        else return false;
        view->GetResource(resource.put());StereoComRef<ID3D11Texture2D> texture;
        if(FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.put()))))return false;
        texture->GetDesc(&desc);return desc.SampleDesc.Count==1&&desc.SampleDesc.Quality==0&&desc.Usage==D3D11_USAGE_DEFAULT&&!desc.CPUAccessFlags;
    }
    bool programs(){
        if(compositeVs_&&compositePs_&&raster_&&depthStates_[0]&&depthStates_[1]&&eyes_[0]&&eyes_[1])return true;
        static constexpr char vs[]=R"(
float4 main(uint i:SV_VertexID):SV_Position {
    float2 p=float2((i<<1)&2,i&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);
})";
        static constexpr char ps[]=R"(
Texture2DArray<float4> Color:register(t0);Texture2DArray<float> Depth:register(t1);
cbuffer Eye:register(b0){uint eye;uint3 padding;};
struct Output {float4 color:SV_Target;float depth:SV_Depth;};
Output main(float4 p:SV_Position){Output o;int4 at=int4(int2(p.xy),int(eye),0);
    o.color=Color.Load(at);o.depth=Depth.Load(at);return o;}
)";
        StereoComRef<ID3DBlob> code,errors;
        if(FAILED(D3DCompile(vs,sizeof(vs)-1,nullptr,nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.put(),errors.put()))||
           FAILED(device_->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,compositeVs_.put())))return false;
        if(FAILED(D3DCompile(ps,sizeof(ps)-1,nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.put(),errors.put()))||
           FAILED(device_->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,compositePs_.put())))return false;
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        if(FAILED(device_->CreateRasterizerState(&raster,raster_.put())))return false;
        for(unsigned eye=0;eye<2;++eye){
            D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
            depth.DepthFunc=eye?D3D11_COMPARISON_GREATER:D3D11_COMPARISON_LESS;
            if(FAILED(device_->CreateDepthStencilState(&depth,depthStates_[eye].put())))return false;
            const uint32_t value[]{eye,0,0,0};D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=16;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;buffer.Usage=D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA initial{value,0,0};if(FAILED(device_->CreateBuffer(&buffer,&initial,eyes_[eye].put())))return false;
        }
        return true;
    }
public:
    StereoOpaqueLayerTargets()=default;
    StereoOpaqueLayerTargets(const StereoOpaqueLayerTargets&)=delete;
    StereoOpaqueLayerTargets& operator=(const StereoOpaqueLayerTargets&)=delete;
    bool prepare(ID3D11DeviceContext* context,const StereoFrameIdentity& key,ID3D11RenderTargetView* nativeColor,
        ID3D11DepthStencilView* nativeDepth,D3D11_COMPARISON_FUNC comparison,uint64_t budget=512ull*1024*1024) noexcept {
        if(identity_.valid()&&identity_.thread!=GetCurrentThreadId()){reason_="foreign opaque layer thread";return false;}
        if(phase_==Phase::Ready||phase_==Phase::Rendered||phase_==Phase::LeftConsumed){reason_="opaque layer still in flight";return false;}
        const auto fail=[&](const char* reason){phase_=Phase::Aborted;approved_=false;reason_=reason;return false;};
        if(!context||!key.valid()||key.thread!=GetCurrentThreadId()||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||
           (comparison!=D3D11_COMPARISON_LESS&&comparison!=D3D11_COMPARISON_GREATER))return fail("unsupported opaque layer scope/depth comparison");
        try {
            StereoComRef<ID3D11Device> device;context->GetDevice(device.put());
            if(!device||device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0||FAILED(device->GetDeviceRemovedReason()))return fail("opaque layer device unavailable");
            if(device_.get()!=device.get()){compositeVs_.reset();compositePs_.reset();raster_.reset();depthStates_={};eyes_={};}
            device_=std::move(device);
            D3D11_TEXTURE2D_DESC color{},depth{};DXGI_FORMAT format{};
            if(!color_view(nativeColor,templateColor_,color,format)||!depth_view(nativeDepth,templateDepth_,depth)||
               !stereo_draw_detail::device_matches(nativeColor,device_.get())||!stereo_draw_detail::device_matches(nativeDepth,device_.get())||
               color.Width!=depth.Width||color.Height!=depth.Height)return fail("opaque layer native target mismatch");
            UINT colorBytes=0;switch(format){
            case DXGI_FORMAT_R16G16B16A16_FLOAT:colorBytes=8;break;
            case DXGI_FORMAT_R32G32B32A32_FLOAT:colorBytes=16;break;
            case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM:colorBytes=4;break;
            default:return fail("opaque layer requires linear full-color targets");}
            if(!color.Width||!color.Height||uint64_t(color.Width)*color.Height*2*(colorBytes+4)>budget)return fail("opaque layer allocation budget");
            if(!programs())return fail("opaque layer composite program creation failed");
            D3D11_TEXTURE2D_DESC array{};array.Width=color.Width;array.Height=color.Height;array.MipLevels=1;array.ArraySize=2;
            array.SampleDesc.Count=1;array.Format=format;array.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            if(FAILED(device_->CreateTexture2D(&array,nullptr,color_.put())))return fail("opaque color array allocation failed");
            array.Format=DXGI_FORMAT_R32_TYPELESS;array.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
            if(FAILED(device_->CreateTexture2D(&array,nullptr,depth_.put())))return fail("opaque depth array allocation failed");
            for(unsigned i=0;i<3;++i){
                const UINT first=i==2?0:i,count=i==2?2:1;
                D3D11_RENDER_TARGET_VIEW_DESC cv{};cv.Format=format;cv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;cv.Texture2DArray.FirstArraySlice=first;cv.Texture2DArray.ArraySize=count;
                D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;dv.Texture2DArray.FirstArraySlice=first;dv.Texture2DArray.ArraySize=count;
                if(FAILED(device_->CreateRenderTargetView(color_.get(),&cv,colorViews_[i].put()))||FAILED(device_->CreateDepthStencilView(depth_.get(),&dv,depthViews_[i].put())))return fail("opaque array view creation failed");
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;srv.Texture2DArray.ArraySize=2;srv.Texture2DArray.MipLevels=1;
            if(FAILED(device_->CreateShaderResourceView(color_.get(),&srv,colorRead_.put())))return fail("opaque color read view failed");
            srv.Format=DXGI_FORMAT_R32_FLOAT;if(FAILED(device_->CreateShaderResourceView(depth_.get(),&srv,depthRead_.put())))return fail("opaque depth read view failed");
            const float clear[4]{};context->ClearRenderTargetView(colorViews_[2].get(),clear);
            context->ClearDepthStencilView(depthViews_[2].get(),D3D11_CLEAR_DEPTH,comparison==D3D11_COMPARISON_LESS?1.f:0.f,0);
            width_=color.Width;height_=color.Height;colorFormat_=format;comparison_=comparison;
            context_=StereoComRef<ID3D11DeviceContext>(context);identity_=key;approved_=false;readSources_.clear();
            phase_=Phase::Ready;reason_="private opaque layer arrays ready";return true;
        }catch(...){return fail("opaque layer allocation failure");}
    }
    bool accepts_packets(const std::array<std::vector<StereoDrawPacket>,2>& input,const StereoFrameIdentity& key) noexcept {
        if(phase_!=Phase::Ready||approved_||identity_!=key||key.thread!=GetCurrentThreadId())return false;
        try {
            std::vector<StereoComRef<ID3D11Resource>> sources;
            for(const auto& eye:input)for(const auto& packet:eye){
                if(!packet.pipeline||!packet.pipeline->fullColorOutput||packet.sampleMask!=0xffffffffu)return false;
                D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;depth.DepthFunc=D3D11_COMPARISON_LESS;
                if(packet.depth)packet.depth->GetDesc(&depth);
                if(!depth.DepthEnable||depth.DepthWriteMask!=D3D11_DEPTH_WRITE_MASK_ALL||depth.DepthFunc!=comparison_||depth.StencilEnable)return false;
                D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
                if(packet.blend)packet.blend->GetDesc(&blend);
                if(blend.AlphaToCoverageEnable||blend.RenderTarget[0].BlendEnable||blend.RenderTarget[0].RenderTargetWriteMask!=D3D11_COLOR_WRITE_ENABLE_ALL)return false;
                if(packet.blend){StereoComRef<ID3D11BlendState1> blend1;
                    if(SUCCEEDED(packet.blend->QueryInterface(__uuidof(ID3D11BlendState1),reinterpret_cast<void**>(blend1.put())))){
                        D3D11_BLEND_DESC1 extended{};blend1->GetDesc1(&extended);if(extended.RenderTarget[0].LogicOpEnable)return false;
                    }
                }
                if(packet.raster){StereoComRef<ID3D11RasterizerState1> raster1;
                    if(SUCCEEDED(packet.raster->QueryInterface(__uuidof(ID3D11RasterizerState1),reinterpret_cast<void**>(raster1.put())))){
                        D3D11_RASTERIZER_DESC1 extended{};raster1->GetDesc1(&extended);if(extended.ForcedSampleCount)return false;
                    }
                }
                for(const auto& stage:packet.stages)for(const auto& binding:stage.resources)if(binding.resource){
                    auto* source=binding.resource.source_resource();
                    if(source==templateColor_.get()||source==templateDepth_.get()||!source)return false;
                    if(std::none_of(sources.begin(),sources.end(),[&](const auto& other){return other.get()==source;}))sources.emplace_back(source);
                }
            }
            readSources_=std::move(sources);approved_=true;return true;
        }catch(...){return false;}
    }
    ID3D11RenderTargetView* color_target(unsigned eye)const{return eye<3&&phase_!=Phase::Empty&&phase_!=Phase::Aborted?colorViews_[eye].get():nullptr;}
    ID3D11DepthStencilView* depth_target(unsigned eye)const{return eye<3&&phase_!=Phase::Empty&&phase_!=Phase::Aborted?depthViews_[eye].get():nullptr;}
    bool matches(ID3D11DeviceContext* context,const StereoFrameIdentity& key)const{return context_.get()==context&&identity_==key&&key.thread==GetCurrentThreadId()&&phase_==Phase::Ready;}
    bool mark_rendered(const StereoFrameIdentity& key){if(!approved_||identity_!=key||key.thread!=GetCurrentThreadId()||phase_!=Phase::Ready)return false;phase_=Phase::Rendered;return true;}
    bool consume(unsigned eye,const StereoFrameIdentity& key,ID3D11RenderTargetView* nativeColor,ID3D11DepthStencilView* nativeDepth,
        StereoQueryBoundary queries=StereoQueryBoundary::Unestablished) noexcept {
        if(eye>1||identity_!=key||key.thread!=GetCurrentThreadId()||queries!=StereoQueryBoundary::OutsideResultScopes||
           (eye==0?phase_!=Phase::Rendered:phase_!=Phase::LeftConsumed))return false;
        try {
            D3D11_TEXTURE2D_DESC color{},depth{};DXGI_FORMAT format{};StereoComRef<ID3D11Resource> cr,dr;
            if(!color_view(nativeColor,cr,color,format)||!depth_view(nativeDepth,dr,depth)||format!=colorFormat_||
               color.Width!=width_||color.Height!=height_||depth.Width!=width_||depth.Height!=height_||
               !stereo_draw_detail::device_matches(nativeColor,device_.get())||!stereo_draw_detail::device_matches(nativeDepth,device_.get()))return false;
            for(const auto& source:readSources_)if(source.get()==cr.get()||source.get()==dr.get())return false;
            StereoComRef<ID3D11Predicate> predicate;BOOL predicated=FALSE;context_->GetPredication(predicate.put(),&predicated);if(predicate)return false;
            std::array<ID3D11RenderTargetView*,8> current{};StereoComRef<ID3D11DepthStencilView> currentDepth;
            context_->OMGetRenderTargets(UINT(current.size()),current.data(),currentDepth.put());
            bool bound=current[0]==nativeColor&&currentDepth.get()==nativeDepth;
            for(unsigned i=0;i<current.size();++i){if(i&&current[i])bound=false;if(current[i])current[i]->Release();}if(!bound)return false;
            StereoComRef<ID3D11DeviceContext> deferred;if(FAILED(device_->CreateDeferredContext(0,deferred.put())))return false;
            deferred->ClearState();deferred->OMSetRenderTargets(1,&nativeColor,nativeDepth);
            deferred->OMSetDepthStencilState(depthStates_[comparison_==D3D11_COMPARISON_GREATER?1:0].get(),0);
            deferred->RSSetState(raster_.get());const D3D11_VIEWPORT viewport{0,0,float(width_),float(height_),0,1};deferred->RSSetViewports(1,&viewport);
            deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            deferred->VSSetShader(compositeVs_.get(),nullptr,0);deferred->PSSetShader(compositePs_.get(),nullptr,0);
            const std::array<ID3D11ShaderResourceView*,2> views{colorRead_.get(),depthRead_.get()};deferred->PSSetShaderResources(0,2,views.data());
            auto* cb=eyes_[eye].get();deferred->PSSetConstantBuffers(0,1,&cb);deferred->Draw(3,0);
            StereoComRef<ID3D11CommandList> commands;
            if(FAILED(deferred->FinishCommandList(FALSE,commands.put()))||FAILED(device_->GetDeviceRemovedReason()))return false;
            context_->ExecuteCommandList(commands.get(),TRUE);phase_=eye?Phase::Complete:Phase::LeftConsumed;
            reason_="opaque layer consumed against actual native depth";return true;
        }catch(...){return false;}
    }
    void abort(){if(!identity_.valid()||identity_.thread==GetCurrentThreadId())phase_=Phase::Aborted;}
    bool complete()const{return phase_==Phase::Complete;}
    const char* reason()const{return reason_;}
};
}
