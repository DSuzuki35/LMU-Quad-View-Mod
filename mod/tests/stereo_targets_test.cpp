#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../stereo_targets.hpp"
#include <d3dcompiler.h>
#include <cassert>
#include <iostream>
#include <thread>
using namespace lmuqv;
uint32_t color_value(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* source){
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);StereoComRef<ID3D11Texture2D> resolved;
    if(desc.SampleDesc.Count>1){auto single=desc;single.ArraySize=single.MipLevels=single.SampleDesc.Count=1;single.SampleDesc.Quality=0;single.BindFlags=0;
        assert(SUCCEEDED(d->CreateTexture2D(&single,nullptr,resolved.put())));c->ResolveSubresource(resolved.get(),0,source,0,desc.Format);source=resolved.get();desc=single;}
    desc.ArraySize=desc.MipLevels=1;desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    StereoComRef<ID3D11Texture2D> staging;assert(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,staging.put())));c->CopySubresourceRegion(staging.get(),0,0,0,0,source,0,nullptr);
    D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(c->Map(staging.get(),0,D3D11_MAP_READ,0,&map)));uint32_t result=0;std::memcpy(&result,map.pData,4);c->Unmap(staging.get(),0);return result;
}
void run(UINT samples){
    StereoComRef<ID3D11Device> d;StereoComRef<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,d.put(),&level,c.put())));
    D3D11_TEXTURE2D_DESC cd{};cd.Width=cd.Height=8;cd.ArraySize=2;cd.MipLevels=1;cd.SampleDesc.Count=samples;cd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;cd.BindFlags=D3D11_BIND_RENDER_TARGET;
    auto dd=cd;dd.Format=DXGI_FORMAT_R32_TYPELESS;dd.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
    StereoComRef<ID3D11Texture2D> seedColor,seedDepth,bankColor,bankDepth;
    assert(SUCCEEDED(d->CreateTexture2D(&cd,nullptr,seedColor.put()))&&SUCCEEDED(d->CreateTexture2D(&dd,nullptr,seedDepth.put())));
    assert(SUCCEEDED(d->CreateTexture2D(&cd,nullptr,bankColor.put()))&&SUCCEEDED(d->CreateTexture2D(&dd,nullptr,bankDepth.put())));
    std::array<StereoComRef<ID3D11RenderTargetView>,2> seedCv;std::array<StereoComRef<ID3D11DepthStencilView>,2> seedDv;
    StereoComRef<ID3D11RenderTargetView> bankCv;StereoComRef<ID3D11DepthStencilView> bankDv;
    for(unsigned eye=0;eye<2;++eye){D3D11_RENDER_TARGET_VIEW_DESC cv{};cv.Format=cd.Format;cv.ViewDimension=samples>1?D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        if(samples>1){cv.Texture2DMSArray.ArraySize=1;cv.Texture2DMSArray.FirstArraySlice=eye;}else{cv.Texture2DArray.ArraySize=1;cv.Texture2DArray.FirstArraySlice=eye;}
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=samples>1?D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        if(samples>1){dv.Texture2DMSArray.ArraySize=1;dv.Texture2DMSArray.FirstArraySlice=eye;}else{dv.Texture2DArray.ArraySize=1;dv.Texture2DArray.FirstArraySlice=eye;}
        assert(SUCCEEDED(d->CreateRenderTargetView(seedColor.get(),&cv,seedCv[eye].put()))&&SUCCEEDED(d->CreateDepthStencilView(seedDepth.get(),&dv,seedDv[eye].put())));
        if(!eye){assert(SUCCEEDED(d->CreateRenderTargetView(bankColor.get(),&cv,bankCv.put()))&&SUCCEEDED(d->CreateDepthStencilView(bankDepth.get(),&dv,bankDv.put())));}
        const float color[4]{float(eye),0,float(!eye),1};c->ClearRenderTargetView(seedCv[eye].get(),color);c->ClearDepthStencilView(seedDv[eye].get(),D3D11_CLEAR_DEPTH,.25f+eye*.5f,0);
    }
    StereoFrameIdentity key{1,2,3,400,GetCurrentThreadId()};StereoGpuResourcePool pool;assert(pool.begin(c.get(),key));
    const auto frozenColor=pool.freeze(seedColor.get()),frozenDepth=pool.freeze(seedDepth.get());assert(frozenColor&&frozenDepth);
    std::array<StereoEyeTargetInput,2> input;
    for(unsigned eye=0;eye<2;++eye){input[eye].colorSeed=frozenColor;input[eye].depthSeed=frozenDepth;input[eye].colorSubresource=input[eye].depthSubresource=eye;
        input[eye].colorDestination=bankCv;input[eye].depthDestination=bankDv;}
    StereoTargetBridge bridge;assert(bridge.prepare(c.get(),key,input));assert(bridge.matches(c.get(),key)&&bridge.width()==8&&bridge.samples()==samples);
    assert(!bridge.prepare(c.get(),key,input)&&!bridge.consume(0,key));
    assert(bridge.mark_rendered(key)&&!bridge.consume(1,key)&&bridge.consume(0,key));
    assert(color_value(d.get(),c.get(),bankColor.get())==0xffff0000u); // blue left seed
    const float overwrite[4]{0,1,0,1};c->ClearRenderTargetView(bankCv.get(),overwrite);c->ClearDepthStencilView(bankDv.get(),D3D11_CLEAR_DEPTH,0,0);
    bool foreign=true;std::thread worker([&]{foreign=bridge.consume(1,key);});worker.join();assert(!foreign);
    assert(bridge.consume(1,key)&&bridge.complete()&&!bridge.consume(1,key));
    assert(color_value(d.get(),c.get(),bankColor.get())==0xff0000ffu); // right seed survived bank reuse
    // Read every sample of copied depth through a typed SRV; CopySubresourceRegion
    // must retain MSAA samples, with no depth resolve or single-sample shortcut.
    const char* shader=samples>1?R"(Texture2DMSArray<float> D:register(t0);RWStructuredBuffer<float> O:register(u0);[numthreads(1,1,1)]void main(uint3 p:SV_DispatchThreadID){O[p.x]=D.Load(int3(0,0,0),p.x);})":
        R"(Texture2DArray<float> D:register(t0);RWStructuredBuffer<float> O:register(u0);[numthreads(1,1,1)]void main(uint3 p:SV_DispatchThreadID){O[p.x]=D.Load(int4(0,0,0,0));})";
    StereoComRef<ID3DBlob> code,error;assert(SUCCEEDED(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,code.put(),error.put())));
    StereoComRef<ID3D11ComputeShader> cs;assert(SUCCEEDED(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,cs.put())));
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R32_FLOAT;sd.ViewDimension=samples>1?D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    if(samples>1)sd.Texture2DMSArray.ArraySize=1;else{sd.Texture2DArray.ArraySize=1;sd.Texture2DArray.MipLevels=1;}
    StereoComRef<ID3D11ShaderResourceView> srv;assert(SUCCEEDED(d->CreateShaderResourceView(bankDepth.get(),&sd,srv.put())));
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=samples*4;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=4;
    StereoComRef<ID3D11Buffer> values;assert(SUCCEEDED(d->CreateBuffer(&bd,nullptr,values.put())));D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=samples;
    StereoComRef<ID3D11UnorderedAccessView> uav;assert(SUCCEEDED(d->CreateUnorderedAccessView(values.get(),&ud,uav.put())));
    auto* r=srv.get();auto* u=uav.get();c->CSSetShader(cs.get(),nullptr,0);c->CSSetShaderResources(0,1,&r);c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->Dispatch(samples,1,1);
    r=nullptr;u=nullptr;c->CSSetShaderResources(0,1,&r);c->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    bd.Usage=D3D11_USAGE_STAGING;bd.BindFlags=bd.MiscFlags=bd.StructureByteStride=0;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    StereoComRef<ID3D11Buffer> read;assert(SUCCEEDED(d->CreateBuffer(&bd,nullptr,read.put())));c->CopyResource(read.get(),values.get());D3D11_MAPPED_SUBRESOURCE map{};
    assert(SUCCEEDED(c->Map(read.get(),0,D3D11_MAP_READ,0,&map)));for(unsigned sample=0;sample<samples;++sample)assert(static_cast<const float*>(map.pData)[sample]==.75f);c->Unmap(read.get(),0);
    auto stale=key;++stale.frame;assert(!bridge.consume(0,stale));
    StereoTargetBridge bad;auto alias=input;alias[1].colorSubresource=0;assert(!bad.prepare(c.get(),key,alias)&&!bad.color_target(0));
    assert(!bad.prepare(c.get(),key,input,1));bad.abort();assert(!bad.mark_rendered(key));c->ClearState();
}
int main(){for(UINT samples:{1u,2u,4u})run(samples);std::cout<<"Stereo target bridge WARP: independent color/depth seeds, reused native bank, consumer order, all 1x/2x/4x depth samples, stale/thread/alias/budget guards passed\n";}
