#include "stereo_test_fixture.hpp"
#include "../native_stereo_draw_inputs.hpp"
#include "../stereo_batch.hpp"
#include <thread>
using namespace lmuqv;
namespace {
const char* capturePs=R"(
cbuffer Frame:register(b3){float4 scale;};
Texture2D<float4> T:register(t0);SamplerState S:register(s0);
float4 main(float4 pos:SV_Position,float3 color:COLOR0):SV_Target{
    return float4(color*scale.rgb+T.SampleLevel(S,float2(.5,.5),0).rgb*.125,1);
})";
template<class T>std::vector<uint8_t> bytes(const T& value){const auto* p=reinterpret_cast<const uint8_t*>(&value);return {p,p+sizeof(value)};}
std::vector<uint8_t> shader(const char* source,const char* profile){auto* code=compile(source,profile);const auto* p=static_cast<const uint8_t*>(code->GetBufferPointer());std::vector<uint8_t> out(p,p+code->GetBufferSize());drop(code);return out;}
Image read(Fixture& f){
    Image out;const auto copy=[&](ID3D11Texture2D* source,unsigned eye,auto& values){
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.ArraySize=1;desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        StereoComRef<ID3D11Texture2D> staging;assert(SUCCEEDED(f.d->CreateTexture2D(&desc,nullptr,staging.put())));
        f.c->CopySubresourceRegion(staging.get(),0,0,0,0,source,eye,nullptr);D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(f.c->Map(staging.get(),0,D3D11_MAP_READ,0,&map)));
        values.resize(f.width*f.width);for(UINT y=0;y<f.width;++y)std::memcpy(values.data()+y*f.width,static_cast<const uint8_t*>(map.pData)+y*map.RowPitch,f.width*4);f.c->Unmap(staging.get(),0);
    };
    for(unsigned eye=0;eye<2;++eye){copy(f.color,eye,out.color[eye]);copy(f.depth,eye,out.depth[eye]);}return out;
}
void empty(const StereoDrawCaptureResult& out){assert(!out.complete&&!out.packet.pipeline&&out.packet.vertices.empty()&&!out.packet.indices);}
std::array<std::pair<uintptr_t,size_t>,2> cpuSpans{};
bool readable(const void* pointer,size_t bytes){const auto p=reinterpret_cast<uintptr_t>(pointer);
    for(const auto& span:cpuSpans)if(p>=span.first&&p-span.first<=span.second&&bytes<=span.second-(p-span.first))return true;return false;}
template<class T>void put(std::vector<uint8_t>& target,size_t offset,const T& value){std::memcpy(target.data()+offset,&value,sizeof(value));}
void run(bool geometry,bool hardware){
    Fixture f(hardware);StereoPipelineCache pipelines;assert(pipelines.configure(f.d,geometry));
    const std::vector<StereoInputElement> layout{{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0},{"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12}};
    const auto pixelCode=shader(capturePs,"ps_4_0");
    auto pipeline=pipelines.get(shader(vsSource,"vs_4_0"),pixelCode,17,8,layout);assert(pipeline);
    drop(f.referencePs);assert(SUCCEEDED(f.d->CreatePixelShader(pixelCode.data(),pixelCode.size(),nullptr,&f.referencePs)));
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const uint32_t texel=0xff408020;D3D11_SUBRESOURCE_DATA initial{&texel,4,0};StereoComRef<ID3D11Texture2D> texture;
    assert(SUCCEEDED(f.d->CreateTexture2D(&td,&initial,texture.put())));StereoComRef<ID3D11ShaderResourceView> view;
    assert(SUCCEEDED(f.d->CreateShaderResourceView(texture.get(),nullptr,view.put())));
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    StereoComRef<ID3D11SamplerState> sampler;assert(SUCCEEDED(f.d->CreateSamplerState(&sd,sampler.put())));
    const StereoFrameIdentity key{1,2,3,400,GetCurrentThreadId()};StereoGpuResourcePool pool;assert(pool.begin(f.c,key));
    std::array<StereoConstantBuffers,2> constants;
    constants[0][0]=bytes(f.cameras[0]);constants[0][4]=bytes(f.extras[0]);constants[1][3]=bytes(f.scales[0]);
    std::array<std::array<uint64_t,14>,2> tokens{};tokens[0][0]=reinterpret_cast<uintptr_t>(f.camera);
    tokens[0][4]=reinterpret_cast<uintptr_t>(f.extra);tokens[1][3]=reinterpret_cast<uintptr_t>(f.pixel);
    f.c->ClearState();const UINT stride=24,offset=0;f.c->IASetVertexBuffers(0,1,&f.vertices,&stride,&offset);f.c->IASetIndexBuffer(f.indices,DXGI_FORMAT_R32_UINT,0);
    f.c->IASetInputLayout(f.layout);f.c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    f.c->VSSetShader(f.referenceVs,nullptr,0);f.c->PSSetShader(f.referencePs,nullptr,0);
    auto* srv=view.get();auto* sample=sampler.get();f.c->PSSetShaderResources(0,1,&srv);f.c->PSSetSamplers(0,1,&sample);
    f.c->RSSetState(f.raster);D3D11_VIEWPORT viewport{0,0,float(f.width),float(f.width),0,1};f.c->RSSetViewports(1,&viewport);f.c->OMSetDepthStencilState(f.depthState,0);
    f.c->VSSetConstantBuffers(0,1,&f.camera);f.c->VSSetConstantBuffers(4,1,&f.extra);f.c->PSSetConstantBuffers(3,1,&f.pixel);
    f.c->UpdateSubresource(f.camera,0,nullptr,&f.cameras[0],0,0);f.c->UpdateSubresource(f.extra,0,nullptr,f.extras[0],0,0);f.c->UpdateSubresource(f.pixel,0,nullptr,f.scales[0],0,0);
    f.c->OMSetRenderTargets(1,&f.eyeRt[0],f.eyeDs[0]);
    StereoDrawCaptureRequest request;request.identity=key;request.key=10;request.indexCount=6;request.pipeline=pipeline;
    request.vertex=f.referenceVs;request.pixel=f.referencePs;request.layout=f.layout;request.constants=&constants;request.constantTokens=&tokens;
    empty(capture_stereo_draw(f.c,pool,request));request.queries=StereoQueryBoundary::OutsideResultScopes;
    auto captured=capture_stereo_draw(f.c,pool,request);if(!captured.complete)std::cerr<<captured.reason<<'\n';assert(captured.complete);
    assert(captured.packet.vertices.size()==1&&captured.packet.indices.resource()!=f.indices&&captured.packet.vertices[0].buffer.resource()!=f.vertices);
    assert(captured.packet.stages[0].constants[0]==constants[0][0]&&pipelines.size()==1);
    assert(captured.packet.stages[1].resources.size()==1&&captured.packet.stages[1].resources[0].resource.view()!=view.get());
    auto invalid=request;++invalid.identity.frame;empty(capture_stereo_draw(f.c,pool,invalid));
    bool foreign=true;std::thread worker([&]{foreign=capture_stereo_draw(f.c,pool,request).complete;});worker.join();assert(!foreign);
    invalid=request;invalid.vertex=pipeline->monoVs.get();empty(capture_stereo_draw(f.c,pool,invalid));
    const auto snapshots=pool.snapshots_created();++tokens[0][0];empty(capture_stereo_draw(f.c,pool,request));--tokens[0][0];assert(pool.snapshots_created()==snapshots);
    f.c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);empty(capture_stereo_draw(f.c,pool,request));f.c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_QUERY_DESC query{D3D11_QUERY_OCCLUSION_PREDICATE,0};StereoComRef<ID3D11Predicate> predicate;assert(SUCCEEDED(f.d->CreatePredicate(&query,predicate.put())));
    f.c->SetPredication(predicate.get(),FALSE);empty(capture_stereo_draw(f.c,pool,request));f.c->SetPredication(nullptr,FALSE);
    // Native adapter obtains the same bytes from renderer CPU shadows and then
    // validates that their resource tokens match actual GPU bindings.
    std::vector<uint8_t> renderer(0x82d00),image(0x1b1a800);
    for(unsigned i=0;i<nativeStereoVertexConstants.size();++i){const auto& range=nativeStereoVertexConstants[i];put(renderer,range.slot,i);
        const auto buffer=i==0?tokens[0][0]:i==4?tokens[0][4]:0;put(renderer,range.buffer,buffer);}
    const unsigned pixelSlots[]{3,0,1,2,4,6};
    for(unsigned i=0;i<nativeStereoPixelConstants.size();++i){const auto& range=nativeStereoPixelConstants[i];put(renderer,range.slot,pixelSlots[i]);
        put(renderer,range.buffer,i?uint64_t(0):tokens[1][3]);}
    std::memcpy(renderer.data()+nativeStereoVertexConstants[0].data,constants[0][0].data(),constants[0][0].size());
    std::memcpy(renderer.data()+nativeStereoVertexConstants[4].data,constants[0][4].data(),constants[0][4].size());
    std::memcpy(renderer.data()+nativeStereoPixelConstants[0].data,constants[1][3].data(),constants[1][3].size());
    cpuSpans={{{reinterpret_cast<uintptr_t>(renderer.data()),renderer.size()},{reinterpret_cast<uintptr_t>(image.data()),image.size()}}};
    auto native=capture_native_stereo_draw_inputs(f.c,pool,cpuSpans[0].first,cpuSpans[1].first,request,readable);
    if(!native.complete)std::cerr<<native.reason<<'\n';assert(native.complete&&native.packet.stages[0].constants[0].size()==272);
    image[0x1b1a7fd]=1;empty(capture_native_stereo_draw_inputs(f.c,pool,cpuSpans[0].first,cpuSpans[1].first,request,readable));image[0x1b1a7fd]=0;
    // Both eyes of this controlled workload share one frozen preparation's
    // geometry. Only their independently supplied camera constants differ.
    std::array<std::vector<StereoDrawPacket>,2> input;
    for(unsigned eye=0;eye<2;++eye){auto packet=captured.packet;
        packet.stages[0].constants[0]=bytes(f.cameras[eye]);packet.stages[0].constants[4]=bytes(f.extras[eye]);packet.stages[1].constants[3]=bytes(f.scales[eye]);
        input[eye].push_back(std::move(packet));const float clear[4]{eye*.1f,.05f,float(!eye)*.1f,1};
        f.c->ClearRenderTargetView(f.eyeRt[eye],clear);f.c->ClearDepthStencilView(f.eyeDs[eye],D3D11_CLEAR_DEPTH,1,0);
    }
    const auto color=pool.freeze(f.color),depth=pool.freeze(f.depth);assert(color&&depth);
    std::array<StereoEyeTargetInput,2> targetInput;
    for(unsigned eye=0;eye<2;++eye){targetInput[eye].colorSeed=color;targetInput[eye].depthSeed=depth;targetInput[eye].colorSubresource=targetInput[eye].depthSubresource=eye;
        targetInput[eye].colorDestination=StereoComRef<ID3D11RenderTargetView>(f.eyeRt[eye]);targetInput[eye].depthDestination=StereoComRef<ID3D11DepthStencilView>(f.eyeDs[eye]);}
    StereoTargetBridge targets;assert(targets.prepare(f.c,key,targetInput));
    for(unsigned eye=0;eye<2;++eye){f.c->OMSetRenderTargets(1,&f.eyeRt[eye],f.eyeDs[eye]);
        f.c->UpdateSubresource(f.camera,0,nullptr,&f.cameras[eye],0,0);f.c->UpdateSubresource(f.extra,0,nullptr,f.extras[eye],0,0);f.c->UpdateSubresource(f.pixel,0,nullptr,f.scales[eye],0,0);
        f.c->DrawIndexed(6,0,0);}
    f.c->OMSetRenderTargets(0,nullptr,nullptr);const auto expected=read(f);assert(expected.color[0]!=expected.color[1]);
    std::array<float,36> zeros{};f.c->UpdateSubresource(f.vertices,0,nullptr,zeros.data(),0,0);f.c->UpdateSubresource(f.camera,0,nullptr,zeros.data(),0,0);
    const uint32_t black=0;f.c->UpdateSubresource(texture.get(),0,nullptr,&black,4,0);
    StereoBatch batch;assert(batch.prepare(f.c,key,targets,input));assert(batch.statistics().pairedDraws==1);
    assert(batch.execute(f.c,key,targets)&&targets.consume(0,key)&&targets.consume(1,key));const auto actual=read(f);
    assert(actual.color==expected.color&&actual.depth==expected.depth);
    std::cout<<"Owned native draw inputs: bound-state/shadow checks, GPU snapshots, query/thread refusal and exact paired color/depth replay "<<(hardware?"hardware":"WARP")<<" passed\n";
}
}
int main(){
    run(false,false);run(true,false);
    if(std::getenv("LMU_QV_STEREO_HARDWARE_TEST")){run(false,true);run(true,true);}
}
