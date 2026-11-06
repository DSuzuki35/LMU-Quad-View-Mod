#include "stereo_test_fixture.hpp"
#include "../stereo_opaque_layer.hpp"
#include "../stereo_batch.hpp"
#include "../stereo_native_replacement.hpp"
#include "../stereo_native_registry.hpp"
#include "../native_sps_runtime.hpp"
#include <thread>
using namespace lmuqv;
namespace {
template<class T>std::vector<uint8_t> bytes(const T& value){const auto* p=reinterpret_cast<const uint8_t*>(&value);return {p,p+sizeof(value)};}
std::vector<uint8_t> shader(const char* source,const char* profile){auto* code=compile(source,profile);const auto* p=static_cast<const uint8_t*>(code->GetBufferPointer());std::vector<uint8_t> out(p,p+code->GetBufferSize());drop(code);return out;}
struct Output {std::vector<uint8_t> color,depth;bool operator==(const Output& other)const{return color==other.color&&depth==other.depth;}};
Output read(Fixture& f,unsigned eye){
    Output out;const auto copy=[&](ID3D11Texture2D* source,UINT bpp,std::vector<uint8_t>& data){
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.ArraySize=1;desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        StereoComRef<ID3D11Texture2D> staging;assert(SUCCEEDED(f.d->CreateTexture2D(&desc,nullptr,staging.put())));
        f.c->CopySubresourceRegion(staging.get(),0,0,0,0,source,eye,nullptr);D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(f.c->Map(staging.get(),0,D3D11_MAP_READ,0,&map)));
        data.resize(desc.Width*desc.Height*bpp);for(UINT y=0;y<desc.Height;++y)std::memcpy(data.data()+y*desc.Width*bpp,static_cast<const uint8_t*>(map.pData)+y*map.RowPitch,desc.Width*bpp);f.c->Unmap(staging.get(),0);
    };
    D3D11_TEXTURE2D_DESC desc{};f.color->GetDesc(&desc);copy(f.color,desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4,out.color);copy(f.depth,4,out.depth);return out;
}
void seed(Fixture& f,unsigned bank,unsigned eye,bool greater){
    const float color[4]{eye?.125f:.625f,.25f,eye?.75f:.125f,.5f};
    f.c->ClearRenderTargetView(f.eyeRt[bank],color);
    f.c->ClearDepthStencilView(f.eyeDs[bank],D3D11_CLEAR_DEPTH,eye?(greater?.75f:.25f):.5f,0);
}
void prepare_native(Fixture& f,unsigned eye,unsigned bank,ID3D11DepthStencilState* depth){
    f.c->ClearState();const UINT stride=24,offset=0;f.c->IASetVertexBuffers(0,1,&f.vertices,&stride,&offset);f.c->IASetIndexBuffer(f.indices,DXGI_FORMAT_R32_UINT,0);
    f.c->IASetInputLayout(f.layout);f.c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    f.c->VSSetShader(f.referenceVs,nullptr,0);f.c->PSSetShader(f.referencePs,nullptr,0);
    f.c->RSSetState(f.raster);const D3D11_VIEWPORT viewport{0,0,float(f.width),float(f.width),0,1};f.c->RSSetViewports(1,&viewport);f.c->OMSetDepthStencilState(depth,0);
    const float blend[4]{};f.c->OMSetBlendState(nullptr,blend,0xffffffffu);const D3D11_RECT scissor{0,0,LONG(f.width),LONG(f.width)};f.c->RSSetScissorRects(1,&scissor);
    f.c->VSSetConstantBuffers(0,1,&f.camera);f.c->VSSetConstantBuffers(4,1,&f.extra);f.c->PSSetConstantBuffers(3,1,&f.pixel);
    f.c->OMSetRenderTargets(1,&f.eyeRt[bank],f.eyeDs[bank]);
    f.c->UpdateSubresource(f.camera,0,nullptr,&f.cameras[eye],0,0);f.c->UpdateSubresource(f.extra,0,nullptr,f.extras[eye],0,0);f.c->UpdateSubresource(f.pixel,0,nullptr,f.scales[eye],0,0);
}
void reference(Fixture& f,ID3D11DepthStencilState* depth){
    for(unsigned eye=0;eye<2;++eye){prepare_native(f,eye,eye,depth);f.c->DrawIndexed(6,0,0);}
    f.c->OMSetRenderTargets(0,nullptr,nullptr);
}
void run(bool geometry,bool greater,DXGI_FORMAT format,bool hardware){
    Fixture f(hardware,format);StereoNativeRegistry registry;
    const StereoNativePipelineKey nativeKey{f.referenceVs,f.referencePs,f.layout};
    assert(!registry.request(nativeKey)&&!registry.warm_one(f.d,geometry));
    const auto vertexCode=shader(vsSource,"vs_4_0"),pixelCode=shader(psSource,"ps_4_0");
    registry.shader(f.referenceVs,vertexCode.data(),vertexCode.size(),true);registry.shader(f.referencePs,pixelCode.data(),pixelCode.size(),false);
    const D3D11_INPUT_ELEMENT_DESC layout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
    registry.layout(f.layout,layout,2);assert(!registry.request(nativeKey)&&registry.warm_one(f.d,geometry));
    auto pipeline=registry.request(nativeKey);assert(pipeline&&pipeline->fullColorOutput&&registry.ready_count()==1&&!registry.warm_one(f.d,geometry));
    const StereoFrameIdentity key{1,2,3,400,GetCurrentThreadId()};StereoGpuResourcePool pool;assert(pool.begin(f.c,key));
    const auto vertices=pool.freeze(f.vertices),indices=pool.freeze(f.indices);assert(vertices&&indices);
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=greater?D3D11_COMPARISON_GREATER:D3D11_COMPARISON_LESS;
    StereoComRef<ID3D11DepthStencilState> depth;assert(SUCCEEDED(f.d->CreateDepthStencilState(&dd,depth.put())));
    std::array<std::vector<StereoDrawPacket>,2> input;
    for(unsigned eye=0;eye<2;++eye){StereoDrawPacket packet;packet.key=1;packet.pipeline=pipeline;packet.indices=indices;packet.vertices.push_back({0,24,0,vertices});packet.indexCount=6;
        packet.depth=depth;packet.raster=StereoComRef<ID3D11RasterizerState>(f.raster);packet.viewport={0,0,float(f.width),float(f.width),0,1};packet.scissor={0,0,LONG(f.width),LONG(f.width)};
        packet.stages[0].constants[0]=bytes(f.cameras[eye]);packet.stages[0].constants[4]=bytes(f.extras[eye]);packet.stages[1].constants[3]=bytes(f.scales[eye]);input[eye].push_back(std::move(packet));}
    seed(f,0,0,greater);seed(f,1,1,greater);const auto before=read(f,0);
    StereoOpaqueLayerTargets targets;assert(targets.prepare(f.c,key,f.eyeRt[0],f.eyeDs[0],dd.DepthFunc));
    // Domain refusals cannot write any native output or approve the range.
    auto invalid=input;invalid[0][0].sampleMask=0;assert(!targets.accepts_packets(invalid,key));
    invalid=input;D3D11_DEPTH_STENCIL_DESC rejected=dd;rejected.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
    StereoComRef<ID3D11DepthStencilState> badDepth;assert(SUCCEEDED(f.d->CreateDepthStencilState(&rejected,badDepth.put())));invalid[0][0].depth=badDepth;
    assert(!targets.accepts_packets(invalid,key));
    rejected=dd;rejected.DepthFunc=greater?D3D11_COMPARISON_GREATER_EQUAL:D3D11_COMPARISON_LESS_EQUAL;
    assert(SUCCEEDED(f.d->CreateDepthStencilState(&rejected,badDepth.put())));invalid[0][0].depth=badDepth;assert(!targets.accepts_packets(invalid,key));
    invalid=input;D3D11_BLEND_DESC bd{};bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED;
    StereoComRef<ID3D11BlendState> badBlend;assert(SUCCEEDED(f.d->CreateBlendState(&bd,badBlend.put())));invalid[0][0].blend=badBlend;assert(!targets.accepts_packets(invalid,key));
    StereoComRef<ID3D11Device1> device1;D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    if(SUCCEEDED(f.d->QueryInterface(__uuidof(ID3D11Device1),reinterpret_cast<void**>(device1.put())))&&
       SUCCEEDED(f.d->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof(options)))&&options.OutputMergerLogicOp){
        D3D11_BLEND_DESC1 logic{};logic.RenderTarget[0].LogicOpEnable=TRUE;logic.RenderTarget[0].LogicOp=D3D11_LOGIC_OP_XOR;logic.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        StereoComRef<ID3D11BlendState1> state;assert(SUCCEEDED(device1->CreateBlendState1(&logic,state.put())));
        invalid=input;invalid[0][0].blend=StereoComRef<ID3D11BlendState>(state.get());assert(!targets.accepts_packets(invalid,key));
    }
    StereoBatch batch;assert(batch.prepare(f.c,key,targets,input));assert(batch.statistics().pairedDraws==1&&batch.execute(f.c,key,targets));
    assert(read(f,0)==before); // Private rendering never touches the initial bank.
    seed(f,0,0,greater);seed(f,1,1,greater);reference(f,depth.get());const std::array<Output,2> expected{read(f,0),read(f,1)};
    // The actual right initial state is supplied ONLY after paired rendering and
    // left consumption. Both eyes deliberately reuse the same physical bank.
    std::array<Output,2> actual;
    assert(!targets.consume(1,key,f.eyeRt[0],f.eyeDs[0],StereoQueryBoundary::OutsideResultScopes));
    // Exercise the actual suppression decision with independently captured
    // native right inputs. Changed material bytes or resource writes must keep
    // that original right draw, while the already-replaced left stays correct.
    for(unsigned mode=0;mode<4;++mode){
        auto frame=key;frame.frame+=mode+1;StereoGpuResourcePool capturePool;assert(capturePool.begin(f.c,frame));
        StereoNativeReplacement replacement;const float oldScale=f.scales[1][0];
        if(mode==1)f.scales[1][0]=.125f;
        seed(f,0,0,greater);seed(f,1,1,greater);reference(f,depth.get());const std::array<Output,2> nativeExpected{read(f,0),read(f,1)};
        f.scales[1][0]=oldScale;
        std::array<StereoConstantBuffers,2> shadows;
        std::array<std::array<uint64_t,14>,2> ids{};ids[0][0]=reinterpret_cast<uintptr_t>(f.camera);ids[0][4]=reinterpret_cast<uintptr_t>(f.extra);ids[1][3]=reinterpret_cast<uintptr_t>(f.pixel);
        StereoDrawCaptureRequest request;request.identity=frame;request.key=77;request.indexCount=6;request.pipeline=pipeline;
        request.vertex=f.referenceVs;request.pixel=f.referencePs;request.layout=f.layout;request.constants=&shadows;request.constantTokens=&ids;request.queries=StereoQueryBoundary::OutsideResultScopes;
        const auto capture=[&](unsigned eye){shadows[0][0]=bytes(f.cameras[eye]);shadows[0][4]=bytes(f.extras[eye]);shadows[1][3]=bytes(f.scales[eye]);
            auto result=capture_stereo_draw(f.c,capturePool,request);assert(result.complete);return result.packet;};
        seed(f,0,0,greater);prepare_native(f,0,0,depth.get());const auto left=capture(0);auto predicted=left;
        predicted.stages[0].constants[0]=bytes(f.cameras[1]);predicted.stages[0].constants[4]=bytes(f.extras[1]);predicted.stages[1].constants[3]=bytes(f.scales[1]);
        unsigned nativeCalls[2]{};
        if(!replacement.left(f.c,frame,f.eyeRt[0],f.eyeDs[0],left,predicted,StereoQueryBoundary::OutsideResultScopes)){++nativeCalls[0];f.c->DrawIndexed(6,0,0);}
        assert(replacement.pending()&&!nativeCalls[0]&&read(f,0)==nativeExpected[0]);
        if(mode==1)f.scales[1][0]=.125f;
        if(mode==2){const UINT indicesNow[]{0,1,2,3,4,5};f.c->UpdateSubresource(f.indices,0,nullptr,indicesNow,0,0);replacement.resource_written(f.indices);}
        if(mode==3)replacement.unknown_write();
        seed(f,0,1,greater);prepare_native(f,1,0,depth.get());const auto right=capture(1);
        if(!replacement.right(f.c,frame,f.eyeRt[0],f.eyeDs[0],right,StereoQueryBoundary::OutsideResultScopes)){++nativeCalls[1];f.c->DrawIndexed(6,0,0);}
        assert(read(f,0)==nativeExpected[1]&&nativeCalls[1]==(mode?1u:0u));
        const auto& counts=replacement.counts();assert(counts.pairedSubmissions==1&&counts.leftDrawsRemoved==1&&counts.rightDrawsRemoved==(mode?0u:1u)&&counts.rightFallbacks==(mode?1u:0u));
        f.scales[1][0]=oldScale;
    }
    auto stale=key;++stale.frame;
    assert(!targets.consume(0,stale,f.eyeRt[0],f.eyeDs[0],StereoQueryBoundary::OutsideResultScopes));
    for(unsigned eye=0;eye<2;++eye){
        seed(f,0,eye,greater);f.c->OMSetRenderTargets(1,&f.eyeRt[0],f.eyeDs[0]);
        const D3D11_VIEWPORT sentinel{2,3,17,19,.1f,.9f};f.c->RSSetViewports(1,&sentinel);f.c->VSSetShader(f.referenceVs,nullptr,0);
        assert(!targets.consume(eye,key,f.eyeRt[0],f.eyeDs[0]));
        assert(targets.consume(eye,key,f.eyeRt[0],f.eyeDs[0],StereoQueryBoundary::OutsideResultScopes));
        UINT count=1;D3D11_VIEWPORT restored{};f.c->RSGetViewports(&count,&restored);assert(!std::memcmp(&sentinel,&restored,sizeof(sentinel)));
        StereoComRef<ID3D11VertexShader> vs;f.c->VSGetShader(vs.put(),nullptr,nullptr);assert(vs.get()==f.referenceVs);
        actual[eye]=read(f,0);
    }
    assert(targets.complete()&&actual==expected);
    assert(!targets.consume(1,key,f.eyeRt[0],f.eyeDs[0],StereoQueryBoundary::OutsideResultScopes));
    std::cout<<"Opaque paired layer: late independent native seeds, reused bank, exact color/depth/ties, domain refusal and state restoration "
        <<(greater?"GREATER ":"LESS ")<<(format==DXGI_FORMAT_R16G16B16A16_FLOAT?"RGBA16F ":"RGBA8 ")<<(hardware?"hardware":"WARP")<<" passed\n";
}
}
int main(){
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
#endif
    for(bool geometry:{false,true})for(bool greater:{false,true})for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT}){
        run(geometry,greater,format,false);if(std::getenv("LMU_QV_STEREO_HARDWARE_TEST"))run(geometry,greater,format,true);
    }
}
