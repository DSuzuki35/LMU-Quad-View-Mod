#include "stereo_test_fixture.hpp"
#include "../native_sps.hpp"
#include "../native_sps_runtime.hpp"
#include <chrono>
#include <thread>
using namespace lmuqv;
namespace {
const char* nativeVs=R"(
cbuffer Native:register(b0){row_major float4x4 world;row_major float4x4 combined;
    float4 eyePosition;float4 padding;row_major float4x4 current;float4 tail[3];};
cbuffer Material:register(b4){float4 tint;};
struct Out{float4 p:SV_Position;float3 color:COLOR0;};
Out main(float3 p:POSITION,float3 c:COLOR0){Out o;o.p=mul(float4(p,1),combined);
    o.color=c*tint.rgb+eyePosition.xyz*.05+current[0].xyz*.01+world[0].xyz*.01;return o;}
)";
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
struct NativeObjects {
    std::array<uint8_t,0x100> shader{};
    std::array<uint8_t,0xe0> mesh{};
    std::array<uint8_t,0x180> geometry{};
    std::array<uint8_t,0x438> owner{};
    std::array<uint8_t,0x500> view{};
    std::vector<uint8_t> renderer=std::vector<uint8_t>(0x82d00),image=std::vector<uint8_t>(0x1b1a800);
};
NativeObjects* objects=nullptr;
template<class T>void put(void* pointer,size_t offset,const T& value){std::memcpy(static_cast<uint8_t*>(pointer)+offset,&value,sizeof(value));}
bool readable(const void* address,size_t bytes){
    const auto p=reinterpret_cast<uintptr_t>(address);
    const std::pair<uintptr_t,size_t> spans[]={{reinterpret_cast<uintptr_t>(objects),sizeof(*objects)},
        {reinterpret_cast<uintptr_t>(objects->renderer.data()),objects->renderer.size()},
        {reinterpret_cast<uintptr_t>(objects->image.data()),objects->image.size()}};
    for(const auto& span:spans)if(p>=span.first&&p-span.first<=span.second&&bytes<=span.second-(p-span.first))return true;return false;
}
void log(const char* message){std::cout<<message<<'\n';}
void attach(ID3D11Device*,ID3D11DeviceContext* context){assert(native_sps_attach_owned_device(context,reinterpret_cast<uintptr_t>(objects->image.data()),log));}
struct Output {std::vector<uint8_t> color,depth;bool operator==(const Output& other)const{return color==other.color&&depth==other.depth;}};
Output read(Fixture& f){
    Output out;const auto copy=[&](ID3D11Texture2D* source,UINT bpp,std::vector<uint8_t>& data){
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.ArraySize=1;desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        StereoComRef<ID3D11Texture2D> staging;assert(SUCCEEDED(f.d->CreateTexture2D(&desc,nullptr,staging.put())));
        f.c->CopySubresourceRegion(staging.get(),0,0,0,0,source,0,nullptr);D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(f.c->Map(staging.get(),0,D3D11_MAP_READ,0,&map)));
        data.resize(desc.Width*desc.Height*bpp);for(UINT y=0;y<desc.Height;++y)std::memcpy(data.data()+y*desc.Width*bpp,static_cast<const uint8_t*>(map.pData)+y*map.RowPitch,desc.Width*bpp);f.c->Unmap(staging.get(),0);
    };
    D3D11_TEXTURE2D_DESC d{};f.color->GetDesc(&d);copy(f.color,d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4,out.color);copy(f.depth,4,out.depth);return out;
}
void seed(Fixture& f,unsigned eye){const float color[]{eye?.1f:.6f,.2f,eye?.7f:.1f,.5f};
    f.c->ClearRenderTargetView(f.eyeRt[0],color);f.c->ClearDepthStencilView(f.eyeDs[0],D3D11_CLEAR_DEPTH,eye?.6f:.5f,0);}
NativeStereoTransformInput camera(Fixture& f,unsigned eye){NativeStereoTransformInput in;in.world=identity();
    std::copy(f.cameras[eye].matrix,f.cameras[eye].matrix+16,in.view.begin());in.derived=in.view;in.position={eye?.2f:-.3f,0,0};in.localTransform=true;return in;}
void prepare_native(Fixture& f,unsigned eye,bool observe){
    auto& data=*objects;const auto input=camera(f,eye);
    put(data.view.data(),0x1dc,input.view);put(data.view.data(),0x3a0,input.derived);put(data.view.data(),0x220,input.position);
    if(observe){
        nativeSpsRuntime.preparation_begin(reinterpret_cast<uintptr_t>(data.shader.data()),reinterpret_cast<uintptr_t>(data.view.data()),reinterpret_cast<uintptr_t>(data.mesh.data()),0x12f,false,0x51f67e);
        NativeStereoTransformArgs args{data.shader.data(),data.view.data(),data.mesh.data(),nullptr};args.boundary=NativeStereoTransformBoundary::Helper;nativeSpsRuntime.transform(args);
    }
    // Owned native fixture fills the actual CPU shadows and GPU CBs in the
    // pinned layout. Numeric helper equivalence is covered by separate tests.
    const auto transformed=prepare_native_stereo_transform(input);assert(transformed.available);
    auto* common=data.renderer.data()+nativeStereoVertexConstants[0].data;std::memset(common,0,272);
    std::memcpy(common,transformed.constants.world.data(),64);std::memcpy(common+64,transformed.constants.derived.data(),64);
    std::memcpy(common+128,transformed.constants.position.data(),16);std::memcpy(common+160,transformed.constants.view.data(),64);
    const float tint[]{1,1,1,1},scale[]{1,.75f,.5f,1};
    std::memcpy(data.renderer.data()+nativeStereoVertexConstants[4].data,tint,16);
    std::memcpy(data.renderer.data()+nativeStereoPixelConstants[0].data,scale,16);
    if(observe)nativeSpsRuntime.preparation_end(1);
    f.c->UpdateSubresource(f.camera,0,nullptr,common,0,0);f.c->UpdateSubresource(f.extra,0,nullptr,tint,0,0);f.c->UpdateSubresource(f.pixel,0,nullptr,scale,0,0);
    const UINT stride=24,offset=0;f.c->IASetVertexBuffers(0,1,&f.vertices,&stride,&offset);f.c->IASetIndexBuffer(f.indices,DXGI_FORMAT_R32_UINT,0);
    f.c->IASetInputLayout(f.layout);f.c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);f.c->VSSetShader(f.referenceVs,nullptr,0);f.c->PSSetShader(f.referencePs,nullptr,0);
    f.c->VSSetConstantBuffers(0,1,&f.camera);f.c->VSSetConstantBuffers(4,1,&f.extra);f.c->PSSetConstantBuffers(3,1,&f.pixel);
    f.c->OMSetRenderTargets(1,&f.eyeRt[0],f.eyeDs[0]);f.c->OMSetDepthStencilState(f.depthState,0);
    const float blend[4]{};f.c->OMSetBlendState(nullptr,blend,0xffffffffu);f.c->RSSetState(f.raster);
    const D3D11_VIEWPORT viewport{0,0,float(f.width),float(f.width),0,1};f.c->RSSetViewports(1,&viewport);
    const D3D11_RECT scissor{0,0,LONG(f.width),LONG(f.width)};f.c->RSSetScissorRects(1,&scissor);
}
void run(bool hardware,DXGI_FORMAT format){
    NativeObjects data;objects=&data;Fixture f(hardware,format,0,attach);assert(native_sps_device_matches(f.c));
    auto* code=compile(nativeVs,"vs_4_0");drop(f.referenceVs);assert(SUCCEEDED(f.d->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&f.referenceVs)));drop(code);
    drop(f.camera);f.camera=f.buffer(272,D3D11_BIND_CONSTANT_BUFFER);
    put(data.shader.data(),8,uint64_t(1));put(data.shader.data(),0xf8,uint32_t(1));put(data.mesh.data(),0,uint32_t(0x8000));
    put(data.mesh.data(),0x80,uint64_t(reinterpret_cast<uintptr_t>(data.geometry.data())));put(data.geometry.data(),0x130,uint64_t(reinterpret_cast<uintptr_t>(data.owner.data())));put(data.owner.data(),0x298,identity());
    for(unsigned i=0;i<nativeStereoVertexConstants.size();++i){const auto& range=nativeStereoVertexConstants[i];put(data.renderer.data(),range.slot,i);
        put(data.renderer.data(),range.buffer,i==0?uint64_t(reinterpret_cast<uintptr_t>(f.camera)):i==4?uint64_t(reinterpret_cast<uintptr_t>(f.extra)):0);}
    const unsigned pixelSlots[]{3,0,1,2,4,6};
    for(unsigned i=0;i<nativeStereoPixelConstants.size();++i){const auto& range=nativeStereoPixelConstants[i];put(data.renderer.data(),range.slot,pixelSlots[i]);put(data.renderer.data(),range.buffer,i?uint64_t(0):uint64_t(reinterpret_cast<uintptr_t>(f.pixel)));}
    std::array<Output,2> expected;
    for(unsigned eye=0;eye<2;++eye){seed(f,eye);prepare_native(f,eye,false);f.c->DrawIndexed(6,0,0);expected[eye]=read(f);}
    // Finished deferred lists close their query ranges, including a Begin
    // without an explicit End. Execution must preserve known immediate scopes,
    // while its resource writes still invalidate an outstanding stereo pair.
    StereoComRef<ID3D11DeviceContext> deferred;assert(SUCCEEDED(f.d->CreateDeferredContext(0,deferred.put())));
    D3D11_QUERY_DESC deferredDesc{D3D11_QUERY_PIPELINE_STATISTICS,0};StereoComRef<ID3D11Query> deferredQuery;
    assert(SUCCEEDED(f.d->CreateQuery(&deferredDesc,deferredQuery.put())));deferred->Begin(deferredQuery.get());
    const UINT sameIndices[]{0,1,2,3,4,5};deferred->UpdateSubresource(f.indices,0,nullptr,sameIndices,0,0);
    StereoComRef<ID3D11CommandList> commands;assert(SUCCEEDED(deferred->FinishCommandList(FALSE,commands.put())));
    f.c->ExecuteCommandList(commands.get(),TRUE);
    for(unsigned frame=1;frame<=7;++frame){
        NativeStereoViewInputs views;views.complete=true;views.scene=1;views.identity={1,frame,3,400+frame,GetCurrentThreadId()};
        for(unsigned eye=0;eye<2;++eye)views.eyes[eye].transform=camera(f,eye);
        for(unsigned eye=0;eye<2;++eye){seed(f,eye);
            if(frame==3&&eye==1){const UINT sameIndices[]{0,1,2,3,4,5};f.c->UpdateSubresource(f.indices,0,nullptr,sameIndices,0,0);}
            if(frame==4&&eye==1){std::thread worker([&]{const UINT sameIndices[]{0,1,2,3,4,5};f.c->UpdateSubresource(f.indices,0,nullptr,sameIndices,0,0);});worker.join();}
            if(frame==6&&eye==1)f.c->ExecuteCommandList(commands.get(),TRUE);
            {
                NativeSpsGroupScope group(f.c,reinterpret_cast<uintptr_t>(data.renderer.data()),reinterpret_cast<uintptr_t>(data.view.data()),eye,&views,readable);
                prepare_native(f,eye,true);f.c->DrawIndexed(6,0,0);
            }
            assert(read(f)==expected[eye]);
        }
        const auto counts=nativeSpsRuntime.counts();
        if(frame==1)assert(counts.pairedSubmissions==0); // Cache warmup retains native draws.
        if(frame==2)assert(counts.pairedSubmissions==1&&counts.leftDrawsRemoved==1&&counts.rightDrawsRemoved==1);
        if(frame==3)assert(counts.pairedSubmissions==2&&counts.leftDrawsRemoved==2&&counts.rightDrawsRemoved==1&&counts.rightFallbacks==1);
        if(frame==4)assert(counts.pairedSubmissions==3&&counts.leftDrawsRemoved==3&&counts.rightDrawsRemoved==1&&counts.rightFallbacks==2);
        if(frame==5)assert(counts.pairedSubmissions==4&&counts.leftDrawsRemoved==4&&counts.rightDrawsRemoved==2&&counts.rightFallbacks==2);
        if(frame==6)assert(counts.pairedSubmissions==5&&counts.leftDrawsRemoved==5&&counts.rightDrawsRemoved==2&&counts.rightFallbacks==3);
        if(frame==7)assert(counts.pairedSubmissions==6&&counts.leftDrawsRemoved==6&&counts.rightDrawsRemoved==3&&counts.rightFallbacks==3);
    }
    // Native result-query semantics must be preserved: no private draws inside.
    D3D11_QUERY_DESC qd{D3D11_QUERY_PIPELINE_STATISTICS,0};StereoComRef<ID3D11Query> query;assert(SUCCEEDED(f.d->CreateQuery(&qd,query.put())));
    NativeStereoViewInputs views;views.complete=true;views.scene=1;views.identity={1,8,3,408,GetCurrentThreadId()};for(unsigned eye=0;eye<2;++eye)views.eyes[eye].transform=camera(f,eye);
    seed(f,0);f.c->Begin(query.get());f.c->ExecuteCommandList(commands.get(),TRUE);
    {NativeSpsGroupScope group(f.c,reinterpret_cast<uintptr_t>(data.renderer.data()),reinterpret_cast<uintptr_t>(data.view.data()),0,&views,readable);prepare_native(f,0,true);f.c->DrawIndexed(6,0,0);}
    f.c->End(query.get());D3D11_QUERY_DATA_PIPELINE_STATISTICS result{};
    for(unsigned i=0;i<5000&&f.c->GetData(query.get(),&result,sizeof(result),0)==S_FALSE;++i)Sleep(1);
    assert(result.IAVertices==6&&result.IAPrimitives==2&&nativeSpsRuntime.counts().pairedSubmissions==6);
    native_sps_stop();assert(!native_sps_device_matches(f.c));
    std::cout<<"Native SPS live D3D adapter: warmup, real original-draw suppression, exact HDR/depth, mutation fallback, query preservation and teardown "<<(hardware?"hardware":"WARP")<<" passed\n";
}
}
int main(){
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
#endif
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT}){
        run(false,format);if(std::getenv("LMU_QV_STEREO_HARDWARE_TEST"))run(true,format);
    }
}
