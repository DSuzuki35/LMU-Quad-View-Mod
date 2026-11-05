#include "stereo_test_fixture.hpp"
#include "../stereo_batch.hpp"
#include <thread>
using namespace lmuqv;
namespace {
const char* texturedPs=R"(
cbuffer Frame:register(b3){float4 scale;};
cbuffer Material:register(b5){float4 bias;};
Texture2D<float4> T:register(t0);SamplerState S:register(s0);
float4 main(float4 pos:SV_Position,float3 color:COLOR0):SV_Target{return float4(color*scale.rgb+bias.rgb+T.SampleLevel(S,float2(.5,.5),0).rgb*.125,1);}
)";
template<class T> std::vector<uint8_t> bytes(const T& value){const auto* p=reinterpret_cast<const uint8_t*>(&value);return {p,p+sizeof(value)};}
std::vector<uint8_t> shader(const char* source,const char* profile){auto* b=compile(source,profile);const auto* p=static_cast<const uint8_t*>(b->GetBufferPointer());std::vector<uint8_t> result(p,p+b->GetBufferSize());drop(b);return result;}
Image read(Fixture& f){Image result;auto copy=[&](ID3D11Texture2D* source,unsigned eye,auto& values){
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.ArraySize=1;desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    StereoComRef<ID3D11Texture2D> staging;assert(SUCCEEDED(f.d->CreateTexture2D(&desc,nullptr,staging.put())));
    f.c->CopySubresourceRegion(staging.get(),0,0,0,0,source,eye,nullptr);D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(f.c->Map(staging.get(),0,D3D11_MAP_READ,0,&map)));
    values.resize(f.width*f.width);for(UINT y=0;y<f.width;++y)std::memcpy(values.data()+y*f.width,static_cast<const uint8_t*>(map.pData)+y*map.RowPitch,f.width*4);f.c->Unmap(staging.get(),0);
};for(unsigned eye=0;eye<2;++eye){copy(f.color,eye,result.color[eye]);copy(f.depth,eye,result.depth[eye]);}return result;}
void reference(Fixture& f,const std::array<std::vector<StereoDrawPacket>,2>& input){
    f.c->ClearState();const UINT stride=24,offset=0;f.c->IASetVertexBuffers(0,1,&f.vertices,&stride,&offset);f.c->IASetIndexBuffer(f.indices,DXGI_FORMAT_R32_UINT,0);
    f.c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(unsigned eye=0;eye<2;++eye)for(const auto& d:input[eye]){
        f.c->OMSetRenderTargets(1,&f.eyeRt[eye],f.eyeDs[eye]);f.c->OMSetDepthStencilState(d.depth.get(),d.stencilRef);f.c->OMSetBlendState(d.blend.get(),d.blendFactor.data(),d.sampleMask);
        f.c->RSSetState(d.raster.get());f.c->RSSetViewports(1,&d.viewport);f.c->RSSetScissorRects(1,&d.scissor);
        f.c->IASetInputLayout(d.pipeline->monoLayout.get());f.c->VSSetShader(d.pipeline->monoVs.get(),nullptr,0);f.c->PSSetShader(d.pipeline->monoPs.get(),nullptr,0);
        for(unsigned stage=0;stage<2;++stage){const auto& variant=stage?d.pipeline->pixel:d.pipeline->vertex;
            for(unsigned slot=0;slot<14;++slot)if(variant.constantRegisters[slot]){auto* cb=f.buffer(UINT(d.stages[stage].constants[slot].size()),D3D11_BIND_CONSTANT_BUFFER,d.stages[stage].constants[slot].data());
                if(stage)f.c->PSSetConstantBuffers(slot,1,&cb);else f.c->VSSetConstantBuffers(slot,1,&cb);drop(cb);}}
        auto* r=d.stages[1].resources[0].resource.view();auto* s=d.stages[1].samplers[0].state.get();f.c->PSSetShaderResources(0,1,&r);f.c->PSSetSamplers(0,1,&s);
        f.c->DrawIndexedInstanced(d.indexCount,d.instanceCount,d.startIndex,d.baseVertex,d.startInstance);
    }
    f.c->OMSetRenderTargets(0,nullptr,nullptr);
}
void run(bool geometry,bool ordinary,bool hardware){
    Fixture f(hardware);StereoPipelineCache cache;assert(cache.configure(f.d,geometry));
    const std::vector<StereoInputElement> layout{{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0},{"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12}};
    auto pipeline=cache.get(shader(vsSource,"vs_4_0"),shader(texturedPs,"ps_4_0"),17,8,layout);if(!pipeline)std::cerr<<cache.reason()<<'\n';assert(pipeline);
    assert(pipeline->resourceSlots[1][0]==1&&pipeline->pixel.samplerSlots==1&&pipeline->pixel.constantRegisters[5]==1);
    StereoFrameIdentity key{1,2,3,400,GetCurrentThreadId()};StereoGpuResourcePool pool;assert(pool.begin(f.c,key));
    auto vertices=pool.freeze(f.vertices),indices=pool.freeze(f.indices);assert(vertices&&indices);
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const uint32_t pixel=0xff204080;D3D11_SUBRESOURCE_DATA initial{&pixel,4,0};StereoComRef<ID3D11Texture2D> texture;assert(SUCCEEDED(f.d->CreateTexture2D(&td,&initial,texture.put())));
    const auto frozen=pool.freeze(texture.get());D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=td.Format;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
    const auto resource=pool.create_shader_view(frozen,srv);assert(resource);
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    StereoComRef<ID3D11SamplerState> sampler;assert(SUCCEEDED(f.d->CreateSamplerState(&sd,sampler.put())));
    std::array<std::vector<StereoDrawPacket>,2> input;
    for(unsigned eye=0;eye<2;++eye){const uint64_t keys[2][4]={{1,2,3,4},{2,1,3,5}};
        for(unsigned i=0;i<4;++i){StereoDrawPacket d;d.key=keys[eye][i];d.pipeline=pipeline;d.vertices.push_back({0,24,0,vertices});d.indices=indices;d.indexCount=3;d.startIndex=d.key==3?3:0;
            d.depth=StereoComRef<ID3D11DepthStencilState>(f.depthState);d.raster=StereoComRef<ID3D11RasterizerState>(f.raster);d.viewport={0,0,float(f.width),float(f.width),0,1};d.scissor={0,0,LONG(f.width),LONG(f.width)};
            Camera camera=f.cameras[eye];camera.tint[0]=d.key==1?1.f:.2f;camera.tint[1]=d.key==2?1.f:.3f;camera.tint[2]=d.key>=4?1.f:.1f;
            if(d.key>=4)camera.matrix[12]+=eye?.4f:-.4f;
            d.stages[0].constants[0]=bytes(camera);d.stages[0].constants[4]=bytes(f.extras[eye]);d.stages[1].constants[3]=bytes(f.scales[eye]);
            const float material[4]{.02f,.03f,.04f,0};d.stages[1].constants[5]=bytes(material);d.stages[1].resources.push_back({0,resource});d.stages[1].samplers.push_back({0,sampler});
            input[eye].push_back(std::move(d));}
        const float clear[4]{eye*.1f,.05f,float(!eye)*.1f,1};f.c->ClearRenderTargetView(f.eyeRt[eye],clear);f.c->ClearDepthStencilView(f.eyeDs[eye],D3D11_CLEAR_DEPTH,eye?.95f:1.f,0);
    }
    const auto seedColor=pool.freeze(f.color),seedDepth=pool.freeze(f.depth);assert(seedColor&&seedDepth);
    std::array<StereoEyeTargetInput,2> targetInput;
    for(unsigned eye=0;eye<2;++eye){targetInput[eye].colorSeed=seedColor;targetInput[eye].depthSeed=seedDepth;targetInput[eye].colorSubresource=targetInput[eye].depthSubresource=eye;
        targetInput[eye].colorDestination=StereoComRef<ID3D11RenderTargetView>(f.eyeRt[eye]);targetInput[eye].depthDestination=StereoComRef<ID3D11DepthStencilView>(f.eyeDs[eye]);}
    StereoTargetBridge targets;assert(targets.prepare(f.c,key,targetInput));reference(f,input);const auto expected=read(f);
    assert(expected.color[0]!=expected.color[1]);StereoBatch batch;assert(batch.prepare(f.c,key,targets,input,ordinary));
    assert(batch.ready()&&batch.statistics().pairedDraws==2&&batch.statistics().monoDraws==4&&batch.statistics().constantMaps>0);
    assert(!batch.prepare(f.c,key,targets,input));auto stale=key;++stale.frame;assert(!batch.execute(f.c,stale,targets));
    bool foreign=true;std::thread worker([&]{foreign=batch.execute(f.c,key,targets);});worker.join();assert(!foreign);
    // Mutate all original inputs after preparation. Private GPU copies must win.
    std::array<float,36> zeros{};f.c->UpdateSubresource(f.vertices,0,nullptr,zeros.data(),0,0);const uint32_t black=0;f.c->UpdateSubresource(texture.get(),0,nullptr,&black,4,0);
    f.c->VSSetShader(f.referenceVs,nullptr,0);f.c->PSSetShader(f.referencePs,nullptr,0);f.c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    D3D11_VIEWPORT sentinel{2,3,17,19,.1f,.9f};f.c->RSSetViewports(1,&sentinel);f.c->OMSetRenderTargets(1,&f.eyeRt[0],f.eyeDs[0]);
    assert(batch.execute(f.c,key,targets)&&!batch.execute(f.c,key,targets)&&!batch.ready());
    ID3D11VertexShader* restoredVs=nullptr;f.c->VSGetShader(&restoredVs,nullptr,nullptr);assert(restoredVs==f.referenceVs);drop(restoredVs);
    ID3D11PixelShader* restoredPs=nullptr;f.c->PSGetShader(&restoredPs,nullptr,nullptr);assert(restoredPs==f.referencePs);drop(restoredPs);
    D3D11_PRIMITIVE_TOPOLOGY topology{};f.c->IAGetPrimitiveTopology(&topology);assert(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    D3D11_VIEWPORT restored{};UINT n=1;f.c->RSGetViewports(&n,&restored);assert(n==1&&!std::memcmp(&restored,&sentinel,sizeof(restored)));
    assert(targets.consume(0,key)&&targets.consume(1,key));f.c->OMSetRenderTargets(0,nullptr,nullptr);const auto actual=read(f);
    for(unsigned eye=0;eye<2;++eye){if(actual.color[eye]!=expected.color[eye]){size_t differences=0;for(size_t i=0;i<actual.color[eye].size();++i)if(actual.color[eye][i]!=expected.color[eye][i]){if(!differences)std::cerr<<"first pixel="<<i<<" actual="<<std::hex<<actual.color[eye][i]<<" expected="<<expected.color[eye][i]<<std::dec<<'\n';++differences;}
        std::cerr<<"color differences="<<differences<<" eye="<<eye<<" direct="<<pipeline->direct<<" ordinary="<<ordinary<<'\n';}
        assert(actual.color[eye]==expected.color[eye]);assert(actual.depth[eye]==expected.depth[eye]);}
    // Exact compatibility checks: each difference must split a pair.
    const auto& a=input[0][0];auto b=input[1][1];assert(stereo_draw_detail::pairable(a,b));
    b.stages[1].constants[5][0]^=1;assert(!stereo_draw_detail::pairable(a,b));b=input[1][1];b.viewport.TopLeftX=1;assert(!stereo_draw_detail::pairable(a,b));
    b=input[1][1];b.stages[1].resources[0].resource={};assert(!stereo_draw_detail::pairable(a,b));
    b=input[1][1];b.vertices[0].buffer=pool.freeze(f.vertices);assert(!stereo_draw_detail::pairable(a,b));
    StereoTargetBridge rejectTargets;assert(rejectTargets.prepare(f.c,key,targetInput));auto malformed=input;malformed[0][0].stages[1].resources.clear();
    assert(!batch.prepare(f.c,key,rejectTargets,malformed)&&!batch.ready());assert(rejectTargets.matches(f.c,key));
    malformed=input;malformed[0][0].stages[0].constants[0].clear();assert(!batch.prepare(f.c,key,rejectTargets,malformed));
    malformed=input;malformed[0][1].key=malformed[0][0].key;assert(!batch.prepare(f.c,key,rejectTargets,malformed));
    D3D11_QUERY_DESC qd{D3D11_QUERY_OCCLUSION_PREDICATE,0};StereoComRef<ID3D11Predicate> predicate;
    assert(SUCCEEDED(f.d->CreatePredicate(&qd,predicate.put())));f.c->Begin(predicate.get());f.c->End(predicate.get());f.c->SetPredication(predicate.get(),TRUE);
    assert(!batch.prepare(f.c,key,rejectTargets,input));f.c->SetPredication(nullptr,FALSE);
    assert(batch.prepare(f.c,key,rejectTargets,input));f.c->SetPredication(predicate.get(),TRUE);assert(!batch.execute(f.c,key,rejectTargets));
    f.c->SetPredication(nullptr,FALSE);batch.abort();assert(!batch.ready());
    assert(!batch.execute(f.c,key,rejectTargets));rejectTargets.abort();f.c->ClearState();
    std::cout<<"Stereo batch "<<(hardware?"hardware":"WARP")<<" route="<<(pipeline->direct?"vprt":"geometry")<<" constants="<<(ordinary?"ordinary":"offsets")
        <<": crossing orders/unmatched draws, independent eye constants, frozen VB/SRV, exact color/depth, restored context, single-use and refusal guards passed\n";
}
}
int main(int argc,char** argv){const bool hardware=argc>1&&!std::strcmp(argv[1],"--hardware");for(bool geometry:{false,true})for(bool ordinary:{true,false})run(geometry,ordinary,hardware);}
