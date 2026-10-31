#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "stereo_draw_proof.hpp"
#include "stereo_shader.hpp"
#include "stereo_instancing.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace lmuqv {namespace {
template<class T>struct Ref {
    T* p=nullptr;
    Ref()=default;~Ref(){reset();}Ref(const Ref&)=delete;Ref& operator=(const Ref&)=delete;
    Ref(Ref&& other)noexcept:p(other.p){other.p=nullptr;}
    Ref& operator=(Ref&& other)noexcept{if(this!=&other){reset();p=other.p;other.p=nullptr;}return *this;}
    void reset(){if(p)p->Release();p=nullptr;}
};
void check(bool valid,const char* why){if(!valid)throw std::runtime_error(why);}
void okay(HRESULT hr,const char* why){check(SUCCEEDED(hr),why);}
template<class T,size_t N>void bind_refs(const std::array<Ref<T>,N>& refs,T*(&raw)[N]){for(size_t i=0;i<N;++i)raw[i]=refs[i].p;}
template<class T,size_t N>void take_refs(std::array<Ref<T>,N>& refs,T*(&raw)[N]){for(size_t i=0;i<N;++i)refs[i].p=raw[i];}
Ref<ID3D11Buffer> buffer(ID3D11Device* device,UINT bytes,UINT flags){
    D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=flags;Ref<ID3D11Buffer> b;okay(device->CreateBuffer(&d,nullptr,&b.p),"buffer allocation failed");return b;
}
float half(uint16_t value){
    const unsigned sign=value>>15,exponent=(value>>10)&31,mantissa=value&1023;
    if(exponent==31){uint32_t bits=(sign<<31)|0x7f800000u|(mantissa<<13);float result;std::memcpy(&result,&bits,4);return result;}
    const auto magnitude=exponent?std::ldexp(float(1024+mantissa),int(exponent)-25):std::ldexp(float(mantissa),-24);return sign?-magnitude:magnitude;
}
struct Snapshot {
    Ref<ID3D11VertexShader> vs;Ref<ID3D11PixelShader> ps;Ref<ID3D11InputLayout> layout;
    Ref<ID3D11RasterizerState> raster;Ref<ID3D11BlendState> blend;Ref<ID3D11DepthStencilState> depth;
    std::array<Ref<ID3D11Buffer>,32> vb;std::array<UINT,32> stride{},offset{};Ref<ID3D11Buffer> ib;
    DXGI_FORMAT indexFormat{},colorFormat{};UINT indexOffset=0,sampleMask=0,stencil=0;
    FLOAT blendFactor[4]{};D3D11_VIEWPORT viewport{};std::vector<D3D11_RECT> scissor;
    std::array<std::array<Ref<ID3D11Buffer>,14>,2> cb;
    std::array<std::array<Ref<ID3D11ShaderResourceView>,128>,2> srv;
    std::array<std::array<Ref<ID3D11SamplerState>,16>,2> sampler;
    unsigned width=0,height=0;
    void capture(ID3D11Device* device,ID3D11DeviceContext* c){
        check(c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE,"owning immediate context required");
        UINT count=0;c->VSGetShader(&vs.p,nullptr,&count);check(vs.p&&!count,"unsupported vertex class instances");count=0;c->PSGetShader(&ps.p,nullptr,&count);check(ps.p&&!count,"unsupported pixel class instances");
        Ref<ID3D11GeometryShader> gs;Ref<ID3D11HullShader> hs;Ref<ID3D11DomainShader> ds;c->GSGetShader(&gs.p,nullptr,nullptr);c->HSGetShader(&hs.p,nullptr,nullptr);c->DSGetShader(&ds.p,nullptr,nullptr);check(!gs.p&&!hs.p&&!ds.p,"additional shader stages are unsupported");
        Ref<ID3D11Predicate> predicate;BOOL value=FALSE;c->GetPredication(&predicate.p,&value);check(!predicate.p,"predication is unsupported");
        D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);check(topology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,"triangle lists required");
        c->IAGetInputLayout(&layout.p);check(layout.p!=nullptr,"input layout required");ID3D11Buffer* vertices[32]{};c->IAGetVertexBuffers(0,32,vertices,stride.data(),offset.data());take_refs(vb,vertices);
        c->IAGetIndexBuffer(&ib.p,&indexFormat,&indexOffset);check(ib.p&&(indexFormat==DXGI_FORMAT_R16_UINT||indexFormat==DXGI_FORMAT_R32_UINT),"index buffer required");
        c->RSGetState(&raster.p);count=1;c->RSGetViewports(&count,&viewport);check(count==1&&viewport.TopLeftX==0&&viewport.TopLeftY==0,"one origin-aligned viewport required");
        D3D11_RECT rects[16]{};count=16;c->RSGetScissorRects(&count,rects);scissor.assign(rects,rects+count);
        c->OMGetBlendState(&blend.p,blendFactor,&sampleMask);if(blend.p){D3D11_BLEND_DESC desc{};blend.p->GetDesc(&desc);check(!desc.RenderTarget[0].BlendEnable&&!desc.AlphaToCoverageEnable,"opaque non-A2C draw required");}
        c->OMGetDepthStencilState(&depth.p,&stencil);if(depth.p){D3D11_DEPTH_STENCIL_DESC desc{};depth.p->GetDesc(&desc);check(desc.DepthEnable&&desc.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL&&!desc.StencilEnable&&(desc.DepthFunc==D3D11_COMPARISON_LESS||desc.DepthFunc==D3D11_COMPARISON_LESS_EQUAL),"ordinary depth-write state required");}
        ID3D11RenderTargetView* targets[8]{};Ref<ID3D11DepthStencilView> targetDepth;c->OMGetRenderTargets(8,targets,&targetDepth.p);std::array<Ref<ID3D11RenderTargetView>,8> ownedTargets;take_refs(ownedTargets,targets);
        check(targets[0]&&targetDepth.p,"color/depth targets required");for(unsigned i=1;i<8;++i)check(!targets[i],"multiple render targets unsupported");
        D3D11_RENDER_TARGET_VIEW_DESC view{};targets[0]->GetDesc(&view);
        check((view.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D&&view.Texture2D.MipSlice==0)||(view.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DARRAY&&view.Texture2DArray.ArraySize==1&&view.Texture2DArray.MipSlice==0),"single-slice 2D target required");colorFormat=view.Format;
        D3D11_DEPTH_STENCIL_VIEW_DESC nativeDepth{};targetDepth.p->GetDesc(&nativeDepth);check(!nativeDepth.Flags,"read-only native depth view unsupported");
        check(colorFormat==DXGI_FORMAT_R16G16B16A16_FLOAT||colorFormat==DXGI_FORMAT_R32G32B32A32_FLOAT||colorFormat==DXGI_FORMAT_R8G8B8A8_UNORM,"unsupported comparison color format");
        Ref<ID3D11Resource> resource;targets[0]->GetResource(&resource.p);Ref<ID3D11Texture2D> texture;okay(resource.p->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture.p)),"2D texture required");D3D11_TEXTURE2D_DESC td{};texture.p->GetDesc(&td);
        width=td.Width;height=td.Height;check(td.SampleDesc.Count==1&&viewport.Width==float(width)&&viewport.Height==float(height)&&width&&height&&uint64_t(width)*height<=4*1024*1024,"unsupported dimensions/MSAA for bounded proof");
        for(unsigned stage=0;stage<2;++stage){
            ID3D11Buffer* constants[14]{};if(stage==0)c->VSGetConstantBuffers(0,14,constants);else c->PSGetConstantBuffers(0,14,constants);
            std::array<Ref<ID3D11Buffer>,14> source;take_refs(source,constants);
            for(unsigned slot=0;slot<14;++slot)if(source[slot].p){D3D11_BUFFER_DESC desc{};source[slot].p->GetDesc(&desc);check(desc.ByteWidth&&desc.ByteWidth<=65536&&desc.ByteWidth%16==0,"invalid constant buffer size");cb[stage][slot]=buffer(device,desc.ByteWidth,D3D11_BIND_CONSTANT_BUFFER);c->CopyResource(cb[stage][slot].p,source[slot].p);}
            ID3D11ShaderResourceView* resources[128]{};ID3D11SamplerState* samplers[16]{};
            if(stage==0){c->VSGetShaderResources(0,128,resources);c->VSGetSamplers(0,16,samplers);}else{c->PSGetShaderResources(0,128,resources);c->PSGetSamplers(0,16,samplers);}take_refs(srv[stage],resources);take_refs(sampler[stage],samplers);
        }
    }
    bool compatible(const Snapshot& b)const{
        if(width!=b.width||height!=b.height||colorFormat!=b.colorFormat||vs.p!=b.vs.p||ps.p!=b.ps.p||layout.p!=b.layout.p||ib.p!=b.ib.p||indexFormat!=b.indexFormat||indexOffset!=b.indexOffset||stride!=b.stride||offset!=b.offset||raster.p!=b.raster.p||blend.p!=b.blend.p||depth.p!=b.depth.p||sampleMask!=b.sampleMask||stencil!=b.stencil||std::memcmp(blendFactor,b.blendFactor,sizeof(blendFactor))||std::memcmp(&viewport,&b.viewport,sizeof(viewport))||scissor.size()!=b.scissor.size())return false;
        if(!scissor.empty()&&std::memcmp(scissor.data(),b.scissor.data(),scissor.size()*sizeof(D3D11_RECT)))return false;
        for(unsigned i=0;i<32;++i)if(vb[i].p!=b.vb[i].p)return false;
        for(unsigned s=0;s<2;++s){for(unsigned i=0;i<128;++i)if(srv[s][i].p!=b.srv[s][i].p)return false;for(unsigned i=0;i<16;++i)if(sampler[s][i].p!=b.sampler[s][i].p)return false;}return true;
    }
    void bind(ID3D11DeviceContext* c)const{
        ID3D11Buffer* vertices[32]{};bind_refs(vb,vertices);c->IASetVertexBuffers(0,32,vertices,stride.data(),offset.data());c->IASetIndexBuffer(ib.p,indexFormat,indexOffset);c->IASetInputLayout(layout.p);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vs.p,nullptr,0);c->PSSetShader(ps.p,nullptr,0);c->RSSetState(raster.p);c->RSSetViewports(1,&viewport);c->RSSetScissorRects(UINT(scissor.size()),scissor.data());c->OMSetBlendState(blend.p,blendFactor,sampleMask);c->OMSetDepthStencilState(depth.p,stencil);
        for(unsigned s=0;s<2;++s){ID3D11Buffer* constants[14]{};ID3D11ShaderResourceView* resources[128]{};ID3D11SamplerState* samplers[16]{};bind_refs(cb[s],constants);bind_refs(srv[s],resources);bind_refs(sampler[s],samplers);
            if(s==0){c->VSSetConstantBuffers(0,14,constants);c->VSSetShaderResources(0,128,resources);c->VSSetSamplers(0,16,samplers);}else{c->PSSetConstantBuffers(0,14,constants);c->PSSetShaderResources(0,128,resources);c->PSSetSamplers(0,16,samplers);}}
    }
};
}
struct StereoDrawProof::State {
    Ref<ID3D11Device> device;Ref<ID3D11DeviceContext> deferred;DWORD owner=GetCurrentThreadId();
    Snapshot left,right;StereoProofDraw draw;bool haveLeft=false;
    Ref<ID3D11VertexShader> vs;Ref<ID3D11PixelShader> ps;Ref<ID3D11GeometryShader> gs;
    StereoShaderVariant vertex,pixel;
    Ref<ID3D11Texture2D> referenceColor,referenceDepth,pairedColor,pairedDepth;
    std::array<Ref<ID3D11RenderTargetView>,2> referenceRtv;std::array<Ref<ID3D11DepthStencilView>,2> referenceDsv;
    Ref<ID3D11RenderTargetView> pairedRtv;Ref<ID3D11DepthStencilView> pairedDsv;
    void execute(ID3D11DeviceContext* c){Ref<ID3D11CommandList> commands;okay(deferred.p->FinishCommandList(FALSE,&commands.p),"command-list finalization failed");c->ExecuteCommandList(commands.p,TRUE);okay(device.p->GetDeviceRemovedReason(),"device removed during proof");}
    void prepare(ID3D11DeviceContext* c,const StereoProofDraw& source,StereoProofResult& result){
        c->GetDevice(&device.p);okay(device.p->CreateDeferredContext(0,&deferred.p),"deferred context unavailable");const auto caps=query_stereo_capabilities(device.p);check(caps.route!=StereoRoute::unavailable,"portable stereo unsupported");std::string error;
        const bool direct=caps.vertexArrayIndex&&!source.forceGeometry;
        const bool vertexOkay=convert_stereo_shader(source.vertexCode,source.vertexBytes,StereoShaderStage::vertex,source.vertexConstants,direct,vertex,error);check(vertexOkay,error.c_str());
        const bool pixelOkay=convert_stereo_shader(source.pixelCode,source.pixelBytes,StereoShaderStage::pixel,source.pixelConstants,false,pixel,error);check(pixelOkay,error.c_str());
        okay(device.p->CreateVertexShader(vertex.bytecode.data(),vertex.bytecode.size(),nullptr,&vs.p),"converted VS creation failed");okay(device.p->CreatePixelShader(pixel.bytecode.data(),pixel.bytecode.size(),nullptr,&ps.p),"converted PS creation failed");
        if(!direct){Ref<ID3DBlob> code,errors;okay(D3DCompile(vertex.geometrySource.data(),vertex.geometrySource.size(),nullptr,nullptr,nullptr,"main","gs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code.p,&errors.p),"geometry adapter compilation failed");okay(device.p->CreateGeometryShader(code.p->GetBufferPointer(),code.p->GetBufferSize(),nullptr,&gs.p),"geometry adapter creation failed");}
        result.route=stereo_route_name(direct?StereoRoute::vertex_array_index:StereoRoute::geometry_array_index);draw=source;left.capture(device.p,c);result.width=left.width;result.height=left.height;result.key=source.key;
        D3D11_TEXTURE2D_DESC td{};td.Width=left.width;td.Height=left.height;td.ArraySize=2;td.MipLevels=td.SampleDesc.Count=1;td.Format=left.colorFormat;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        okay(device.p->CreateTexture2D(&td,nullptr,&referenceColor.p),"reference color allocation failed");okay(device.p->CreateTexture2D(&td,nullptr,&pairedColor.p),"paired color allocation failed");td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        okay(device.p->CreateTexture2D(&td,nullptr,&referenceDepth.p),"reference depth allocation failed");okay(device.p->CreateTexture2D(&td,nullptr,&pairedDepth.p),"paired depth allocation failed");
        D3D11_RENDER_TARGET_VIEW_DESC rt{};rt.Format=left.colorFormat;rt.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;rt.Texture2DArray.ArraySize=1;
        D3D11_DEPTH_STENCIL_VIEW_DESC ds{};ds.Format=DXGI_FORMAT_D32_FLOAT;ds.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;ds.Texture2DArray.ArraySize=1;
        for(UINT eye=0;eye<2;++eye){rt.Texture2DArray.FirstArraySlice=ds.Texture2DArray.FirstArraySlice=eye;okay(device.p->CreateRenderTargetView(referenceColor.p,&rt,&referenceRtv[eye].p),"reference RTV allocation failed");okay(device.p->CreateDepthStencilView(referenceDepth.p,&ds,&referenceDsv[eye].p),"reference DSV allocation failed");}
        rt.Texture2DArray.FirstArraySlice=ds.Texture2DArray.FirstArraySlice=0;rt.Texture2DArray.ArraySize=ds.Texture2DArray.ArraySize=2;
        okay(device.p->CreateRenderTargetView(pairedColor.p,&rt,&pairedRtv.p),"paired RTV allocation failed");okay(device.p->CreateDepthStencilView(pairedDepth.p,&ds,&pairedDsv.p),"paired DSV allocation failed");
        haveLeft=true;
    }
    void reference(ID3D11DeviceContext* c,unsigned eye){
        auto* dc=deferred.p;dc->ClearState();(eye?right:left).bind(dc);const float zero[4]{};dc->ClearRenderTargetView(referenceRtv[eye].p,zero);dc->ClearDepthStencilView(referenceDsv[eye].p,D3D11_CLEAR_DEPTH,1,0);dc->OMSetRenderTargets(1,&referenceRtv[eye].p,referenceDsv[eye].p);dc->DrawIndexed(draw.indexCount,draw.startIndex,draw.baseVertex);execute(c);
    }
    Ref<ID3D11Buffer> pack(unsigned stage,const StereoShaderVariant& variant){
        const UINT size=variant.eyeStride*2*16;auto plain=buffer(device.p,size,0),packed=buffer(device.p,size,D3D11_BIND_CONSTANT_BUFFER);
        for(unsigned eye=0;eye<2;++eye)for(const auto& range:variant.constants){auto* source=(eye?right:left).cb[stage][range.sourceSlot].p;check(source!=nullptr,"selected constant buffer missing");D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);check(desc.ByteWidth>=range.registers*16,"selected constant buffer too small");
            // Partial copies involving CB resources are forbidden in D3D11.
            // Snapshot to a plain buffer, pack plain regions, then copy the
            // entire packed plain buffer into the equally sized paired CB.
            auto staging=buffer(device.p,desc.ByteWidth,0);deferred.p->CopyResource(staging.p,source);D3D11_BOX box{0,0,0,range.registers*16,1,1};deferred.p->CopySubresourceRegion(plain.p,0,(eye*variant.eyeStride+range.offset)*16,0,0,staging.p,0,&box);
        }deferred.p->CopyResource(packed.p,plain.p);return packed;
    }
    void paired(ID3D11DeviceContext* c){
        auto* dc=deferred.p;dc->ClearState();right.bind(dc);auto vertexCB=pack(0,vertex),pixelCB=pack(1,pixel);dc->VSSetConstantBuffers(vertex.packedSlot,1,&vertexCB.p);dc->PSSetConstantBuffers(pixel.packedSlot,1,&pixelCB.p);
        dc->VSSetShader(vs.p,nullptr,0);dc->PSSetShader(ps.p,nullptr,0);dc->GSSetShader(gs.p,nullptr,0);const float zero[4]{};dc->ClearRenderTargetView(pairedRtv.p,zero);dc->ClearDepthStencilView(pairedDsv.p,D3D11_CLEAR_DEPTH,1,0);dc->OMSetRenderTargets(1,&pairedRtv.p,pairedDsv.p);
        check(submit_stereo_indexed(dc,draw.indexCount,1,draw.startIndex,draw.baseVertex,0),"paired submission failed");execute(c);
    }
    struct Readback {Ref<ID3D11Texture2D> texture;std::array<std::vector<uint8_t>,2> data;unsigned pixelBytes=0;};
    Readback read(ID3D11DeviceContext* c,ID3D11Texture2D* source,unsigned pixelBytes){
        Readback result;result.pixelBytes=pixelBytes;D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;okay(device.p->CreateTexture2D(&d,nullptr,&result.texture.p),"readback allocation failed");c->CopyResource(result.texture.p,source);
        for(unsigned eye=0;eye<2;++eye){auto& data=result.data[eye];data.resize(size_t(d.Width)*d.Height*pixelBytes);D3D11_MAPPED_SUBRESOURCE mapped{};okay(c->Map(result.texture.p,eye,D3D11_MAP_READ,0,&mapped),"readback mapping failed");for(unsigned y=0;y<d.Height;++y)std::memcpy(data.data()+size_t(y)*d.Width*pixelBytes,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(d.Width)*pixelBytes);c->Unmap(result.texture.p,eye);}return result;
    }
    void compare(ID3D11DeviceContext* c,StereoProofResult& result){
        const unsigned pixelBytes=left.colorFormat==DXGI_FORMAT_R16G16B16A16_FLOAT?8:left.colorFormat==DXGI_FORMAT_R32G32B32A32_FLOAT?16:4;
        const auto a=read(c,referenceColor.p,pixelBytes),b=read(c,pairedColor.p,pixelBytes),da=read(c,referenceDepth.p,4),db=read(c,pairedDepth.p,4);
        for(unsigned eye=0;eye<2;++eye)for(size_t i=0;i<size_t(left.width)*left.height;++i){float z0,z1;std::memcpy(&z0,da.data[eye].data()+i*4,4);std::memcpy(&z1,db.data[eye].data()+i*4,4);result.covered[eye]+=z0<1;
            const auto dz=std::fabs(z0-z1);if(!std::isfinite(dz)||dz>1e-6f)++result.depthDifferent[eye];if(std::isfinite(dz))result.maximumDepthError=(std::max)(result.maximumDepthError,dz);
            bool differs=false;for(unsigned channel=0;channel<4;++channel){float x=0,y=0;
                if(pixelBytes==16){std::memcpy(&x,a.data[eye].data()+i*16+channel*4,4);std::memcpy(&y,b.data[eye].data()+i*16+channel*4,4);}
                else if(pixelBytes==8){uint16_t hx,hy;std::memcpy(&hx,a.data[eye].data()+i*8+channel*2,2);std::memcpy(&hy,b.data[eye].data()+i*8+channel*2,2);x=half(hx);y=half(hy);}
                else{x=float(a.data[eye][i*4+channel])/255;y=float(b.data[eye][i*4+channel])/255;}
                const auto delta=std::fabs(x-y);differs|=!std::isfinite(delta)||delta>1e-4f+1e-4f*(std::max)(std::fabs(x),std::fabs(y));if(std::isfinite(delta))result.maximumColorError=(std::max)(result.maximumColorError,delta);
            }result.colorDifferent[eye]+=differs;
        }
        if(result.covered[0]<8||result.covered[1]<8){result.status="inconclusive";result.reason="insufficient visible coverage in sampled draw";}
        else if(result.colorDifferent[0]||result.colorDifferent[1]||result.depthDifferent[0]||result.depthDifferent[1]){result.status="mismatch";result.reason="paired output differs from original references; no game draw suppressed";}
        else{result.status="passed";result.reason="sampled original references match paired private draw; CPU sharing remains disabled";}
    }
};
StereoDrawProof::StereoDrawProof()=default;StereoDrawProof::~StereoDrawProof()=default;
void StereoDrawProof::begin(){reset();state_=std::make_unique<State>();result_.status="armed";}
void StereoDrawProof::reset(){state_.reset();result_={};}
void StereoDrawProof::observe(ID3D11DeviceContext* c,bool right,const StereoProofDraw& draw,bool outsideResultQueries,bool insideTimingQuery){
    if(!state_||(result_.status!="armed"&&result_.status!="waiting_right"))return;++result_.observed;
    if(!outsideResultQueries){++result_.querySkips;return;}
    try{check(c&&GetCurrentThreadId()==state_->owner,"proof called from a different context thread");
        if(!right&&!state_->haveLeft){if(insideTimingQuery)++result_.timingQueryOverlaps;check(draw.key&&draw.indexCount,"native mesh identity and nonempty indexed draw required");state_->prepare(c,draw,result_);state_->reference(c,0);result_.status="waiting_right";}
        else if(right&&state_->haveLeft&&draw.key==state_->draw.key&&draw.indexCount==state_->draw.indexCount&&draw.startIndex==state_->draw.startIndex&&draw.baseVertex==state_->draw.baseVertex){
            if(insideTimingQuery)++result_.timingQueryOverlaps;state_->right.capture(state_->device.p,c);check(state_->left.compatible(state_->right),"geometry, all VS/PS SRV/sampler slots, pipeline or viewport differs between eyes");state_->reference(c,1);state_->paired(c);state_->compare(c,result_);
        }
    }catch(const std::exception& e){result_.status="unavailable";result_.reason=e.what();}catch(...){result_.status="unavailable";result_.reason="proof allocation failed";}
}
void StereoDrawProof::finish(){if(result_.status=="armed"||result_.status=="waiting_right"){result_.reason=result_.status=="armed"?"no eligible draw with safe query state":"no matching right-eye draw";result_.status="inconclusive";}state_.reset();}
}
