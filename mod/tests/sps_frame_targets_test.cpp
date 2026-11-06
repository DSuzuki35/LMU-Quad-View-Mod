#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../sps_frame_targets.hpp"
#include "../stereo_batch.hpp"
#include "../sps_draw_list.hpp"
#include <d3dcompiler.h>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace lmuqv;
namespace {
std::vector<uint8_t> compile(const char* text,const char* profile){
    StereoComRef<ID3DBlob> code,error;
    const auto result=D3DCompile(text,std::strlen(text),nullptr,nullptr,nullptr,"main",profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.put(),error.put());
    if(FAILED(result)&&error)std::cerr<<static_cast<const char*>(error->GetBufferPointer());assert(SUCCEEDED(result));
    const auto* p=static_cast<const uint8_t*>(code->GetBufferPointer());return {p,p+code->GetBufferSize()};
}
const char* targetVertexSource=R"(
cbuffer Object:register(b0){float4 scaleOffset;float4 depthTint;};
cbuffer Material:register(b4){float4 material;};
struct Out{float4 position:SV_Position;float4 color:COLOR0;};
Out main(float2 p:POSITION){Out o;o.position=float4(p*scaleOffset.xy+scaleOffset.zw,depthTint.x,1);
o.color=float4(depthTint.yzw,1)*material;return o;}
)";
const char* targetPixelSource=R"(float4 main(float4 p:SV_Position,float4 c:COLOR0):SV_Target{return c;})";
template<class T>std::vector<uint8_t> bytes(const T& t){const auto* p=reinterpret_cast<const uint8_t*>(&t);return {p,p+sizeof(t)};}
struct Device {
    StereoComRef<ID3D11Device> device;
    StereoComRef<ID3D11DeviceContext> context;
    explicit Device(bool hardware){D3D_FEATURE_LEVEL level{};
        assert(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,device.put(),&level,context.put())));
        assert(level>=D3D_FEATURE_LEVEL_11_0);
    }
    template<class T,size_t N>StereoComRef<ID3D11Buffer> buffer(const T(&data)[N],UINT bind){
        D3D11_BUFFER_DESC d{};d.ByteWidth=sizeof(data);d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA initial{data,0,0};StereoComRef<ID3D11Buffer> result;
        assert(SUCCEEDED(device->CreateBuffer(&d,&initial,result.put())));return result;
    }
};
std::vector<uint8_t> read(Device& d,ID3D11Texture2D* source,UINT subresource,UINT bpp){
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.ArraySize=1;desc.Usage=D3D11_USAGE_STAGING;
    desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;StereoComRef<ID3D11Texture2D> staging;
    assert(SUCCEEDED(d.device->CreateTexture2D(&desc,nullptr,staging.put())));
    d.context->CopySubresourceRegion(staging.get(),0,0,0,0,source,subresource,nullptr);
    D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(d.context->Map(staging.get(),0,D3D11_MAP_READ,0,&map)));
    std::vector<uint8_t> result(desc.Width*desc.Height*bpp);
    for(UINT row=0;row<desc.Height;++row)std::memcpy(result.data()+row*desc.Width*bpp,static_cast<const uint8_t*>(map.pData)+row*map.RowPitch,desc.Width*bpp);
    d.context->Unmap(staging.get(),0);return result;
}
void verify(Device& d,const SpsFrameImage& image,unsigned eye,DXGI_FORMAT format,bool mirrored){
    const UINT bpp=format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4;
    const auto color=read(d,image.color,image.subresource,bpp),depth=read(d,image.depth,image.subresource,4);
    // Independent analytic pixel expectations. There is no reference eye draw,
    // engine callback, captured native target or native pipeline linked here.
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
        const auto side=mirrored?1-eye:eye;
        const bool common=y>=24&&y<40&&x>=(side?32u:16u)&&x<(side?48u:32u);
        const bool separate=y>=28&&y<36&&x>=(side?12u:44u)&&x<(side?20u:52u);
        const std::array<unsigned,4> expected{unsigned(common),unsigned(separate&&!side),unsigned(separate&&side),1};
        const auto index=y*64+x;
        for(unsigned c=0;c<4;++c){
            if(bpp==4)assert(color[index*4+c]==expected[c]*255);
            else{uint16_t value=0;std::memcpy(&value,color.data()+index*8+c*2,2);assert(value==(expected[c]?0x3c00:0));}
        }
        float z=0;std::memcpy(&z,depth.data()+index*4,4);assert(z==(common?.5f:separate?.25f:1.f));
    }
}
void run(bool hardware,bool geometry,DXGI_FORMAT format){
    Device d(hardware);StereoPipelineCache cache;assert(cache.configure(d.device.get(),geometry));
    const auto pipeline=cache.get(compile(targetVertexSource,"vs_4_0"),compile(targetPixelSource,"ps_4_0"),1,0,
        {{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0}});
    if(!pipeline)std::cerr<<cache.reason()<<'\n';assert(pipeline);
    const float points[][2]{{-1,-1},{-1,1},{1,1},{1,-1}};const UINT indices[]{0,1,2,0,2,3};
    auto vertices=d.buffer(points,D3D11_BIND_VERTEX_BUFFER),index=d.buffer(indices,D3D11_BIND_INDEX_BUFFER);
    D3D11_RASTERIZER_DESC rasterDesc{};rasterDesc.FillMode=D3D11_FILL_SOLID;rasterDesc.CullMode=D3D11_CULL_NONE;rasterDesc.DepthClipEnable=TRUE;
    StereoComRef<ID3D11RasterizerState> raster;assert(SUCCEEDED(d.device->CreateRasterizerState(&rasterDesc,raster.put())));
    SpsFrameTargets targets;StereoBatch batch;StereoGpuResourcePool resources;
    StereoResourceLease vertexSnapshot,indexSnapshot;
    auto material=std::make_shared<StereoConstantBuffers>();const float tint[]{1,1,1,1};(*material)[4]=bytes(tint);
    SpsFrameTargetDescription description{64,64,format,DXGI_FORMAT_D32_FLOAT,1,0};SpsFrameClear clear;clear.depth=1;
    for(unsigned frame=1;frame<=2;++frame){
        StereoFrameIdentity identity{1,frame,3,400+frame,GetCurrentThreadId()};
        assert(targets.begin(d.context.get(),identity,description,clear));assert(resources.begin(d.context.get(),identity));
        if(frame==1){vertexSnapshot=resources.freeze(vertices.get());indexSnapshot=resources.freeze(index.get());}
        const auto v=resources.pin(vertexSnapshot),i=resources.pin(indexSnapshot);assert(v&&i);
        assert(resources.copied_bytes()==0&&resources.snapshots_created()==(frame==1?2:0));
        std::array<std::vector<StereoDrawPacket>,2> draws;
        for(unsigned eye=0;eye<2;++eye){const unsigned side=frame==2?1-eye:eye;
            for(unsigned mesh=0;mesh<2;++mesh){StereoDrawPacket draw;draw.key=mesh?200+side:100;draw.pipeline=pipeline;
                draw.vertices.push_back({0,8,0,v});draw.indices=i;draw.indexCount=6;draw.raster=raster;
                draw.viewport={0,0,64,64,0,1};draw.scissor={0,0,64,64};
                const float constants[8]{mesh?.125f:.25f,mesh?.125f:.25f,mesh?(side?-.5f:.5f):(side?.25f:-.25f),0,
                    mesh?.25f:.5f,float(!mesh),float(mesh&&!side),float(mesh&&side)};
                draw.stages[0].constants[0]=bytes(constants);draw.stages[0].sharedConstants=material;draws[eye].push_back(std::move(draw));
            }
        }
        std::array<SpsDrawList,2> ordered;for(unsigned eye=0;eye<2;++eye)for(const auto& draw:draws[eye])ordered[eye].push_back(draw);
        assert(batch.prepare(d.context.get(),identity,targets,ordered));
        assert(batch.statistics().pairedDraws==1&&batch.statistics().monoDraws==2);
        assert(batch.execute(d.context.get(),identity,targets));assert(!batch.execute(d.context.get(),identity,targets));
        for(unsigned eye=0;eye<2;++eye){SpsFrameImage image;assert(targets.image(eye,identity,image));
            assert(image.sampledColor);verify(d,image,eye,format,frame==2);
            auto stale=identity;++stale.frame;assert(!targets.image(eye,stale,image));
            assert(targets.consumed(eye,identity));assert(!targets.consumed(eye,identity));
        }
        assert(targets.allocations()==1);assert(!targets.begin(d.context.get(),identity,description,clear));
    }
    StereoFrameIdentity next{1,3,3,403,GetCurrentThreadId()};auto invalid=description;invalid.width=0;
    assert(!targets.begin(d.context.get(),next,invalid,clear));assert(targets.begin(d.context.get(),next,description,clear));
    SpsFrameImage image;assert(!targets.image(0,next,image));targets.abort();assert(!targets.image(0,next,image));
    std::cout<<"SPS-only frame targets: analytic complete-eye color/depth, full list submission, per-eye membership, stale-frame rejection, allocation reuse; zero native rendering "
        <<(hardware?"hardware":"WARP")<<' '<<(geometry?"GS":"VPRT")<<' '<<(format==DXGI_FORMAT_R16G16B16A16_FLOAT?"HDR":"RGBA8")<<" passed\n";
}
}
int main(){
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
#endif
    for(bool geometry:{false,true})for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT}){
        run(false,geometry,format);if(std::getenv("LMU_QV_STEREO_HARDWARE_TEST"))run(true,geometry,format);
    }
    return 0;
}
