// Reuse SPS-only device/upload/readback helpers, never a reference renderer.
#define main sps_target_helper_main
#include "sps_frame_targets_test.cpp"
#undef main
#include "../sps_renderer.hpp"
#include "../sps_game_asset.hpp"

namespace {
const char* sceneVertex=R"(
cbuffer Object:register(b0){row_major float4x4 world;row_major float4x4 wvp;float4 eye;float4 tint;row_major float4x4 worldView;float4 extra[3];};
cbuffer Skinning:register(b2){row_major float4x4 bones[40];};
struct Out{float4 position:SV_Position;float4 color:COLOR0;float2 uv:TEXCOORD0;};
Out main(float2 p:POSITION){Out o;o.position=mul(mul(float4(p,0,1),bones[0]),wvp);o.color=tint;o.uv=p*.5+.5;return o;}
)";
const char* scenePixel=R"(
Texture2D<float4> image:register(t0);SamplerState pointSampler:register(s0);
float4 main(float4 p:SV_Position,float4 color:COLOR0,float2 uv:TEXCOORD0):SV_Target{return color*image.SampleLevel(pointSampler,uv,0);}
)";
Matrix instance_world(float x,float z,float scale){return {scale,0,0,0,0,scale,0,0,0,0,1,0,x,0,z,1};}
struct AnalyticObject {float x,z,scale;std::array<float,4> color;bool transparent=false;float y=0;};
void verify_scene(Device& d,const SpsFrameImage& image,unsigned view,DXGI_FORMAT format,bool mirrored){
    const UINT bpp=format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4;
    const auto pixels=read(d,image.color,image.subresource,bpp),depth=read(d,image.depth,image.subresource,4);
    const float eye=view%2?.2f:-.2f,tangent=view<2?1.f:.5f;
    std::vector<AnalyticObject> objects{{0,2,.25f,{1,0,0,1}},{mirrored?-.6f:.6f,1,.05f,{0,1,0,1}},
        {-.1f,1.8f,.3f,{0,0,1,.5f},true},{.1f,1.8f,.3f,{1,1,0,.5f},true}};
    if(mirrored)objects[0].y=.0625f; // Updated bone pose, then instance scale.
    const auto distance=[&](const AnalyticObject& o){return (o.x-eye)*(o.x-eye)+o.z*o.z;};
    if(distance(objects[2])<distance(objects[3]))std::swap(objects[2],objects[3]);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
        const float nx=(float(x)+.5f)/32-1,ny=1-(float(y)+.5f)/32;
        std::array<float,4> color{0,0,0,1};float storedDepth=0;
        for(const auto& o:objects){
            const float left=(o.x-o.scale-eye)/(o.z*tangent),right=(o.x+o.scale-eye)/(o.z*tangent),height=o.scale/(o.z*tangent),centerY=o.y/(o.z*tangent);
            if(nx<left||nx>right||ny<centerY-height||ny>centerY+height)continue;
            const float z=-.1f/(100-.1f)+(.1f*100/(100-.1f))/o.z;if(z<=storedDepth)continue;
            if(!o.transparent){color=o.color;storedDepth=z;}
            else for(unsigned channel=0;channel<3;++channel){color[channel]=o.color[channel]*.5f+color[channel]*.5f;
                if(bpp==4)color[channel]=std::round(color[channel]*255)/255;}
        }
        const size_t at=y*64+x;for(unsigned channel=0;channel<4;++channel){
            if(bpp==4){const int expected=int(std::round(color[channel]*255));assert(std::abs(int(pixels[at*4+channel])-expected)<=1);}
            else{uint16_t actual=0;std::memcpy(&actual,pixels.data()+at*8+channel*2,2);
                const uint16_t expected=color[channel]==0?0:color[channel]==.25f?0x3400:color[channel]==.5f?0x3800:color[channel]==.75f?0x3a00:0x3c00;
                assert(actual==expected);}
        }
        float actual=0;std::memcpy(&actual,depth.data()+at*4,4);assert(std::abs(actual-storedDepth)<1e-6f);
    }
}
void scene_run(bool hardware,bool geometry,DXGI_FORMAT format){
    Device d(hardware);StereoNativeRegistry programs;
    const auto vsCode=compile(sceneVertex,"vs_4_0"),psCode=compile(scenePixel,"ps_4_0");
    StereoComRef<ID3D11VertexShader> sourceVs;StereoComRef<ID3D11PixelShader> sourcePs;StereoComRef<ID3D11InputLayout> sourceLayout;
    assert(SUCCEEDED(d.device->CreateVertexShader(vsCode.data(),vsCode.size(),nullptr,sourceVs.put())));
    assert(SUCCEEDED(d.device->CreatePixelShader(psCode.data(),psCode.size(),nullptr,sourcePs.put())));
    const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    assert(SUCCEEDED(d.device->CreateInputLayout(&element,1,vsCode.data(),vsCode.size(),sourceLayout.put())));
    programs.shader(sourceVs.get(),vsCode.data(),vsCode.size(),true);programs.shader(sourcePs.get(),psCode.data(),psCode.size(),false);programs.layout(sourceLayout.get(),&element,1);
    const float points[][2]{{-1,-1},{-1,1},{1,1},{1,-1}};const UINT indices[]{0,1,2,0,2,3,0,1,3,2};
    auto vertices=d.buffer(points,D3D11_BIND_VERTEX_BUFFER),index=d.buffer(indices,D3D11_BIND_INDEX_BUFFER);
    StereoGpuResourcePool imported;StereoFrameIdentity importKey{9,1,3,400,GetCurrentThreadId()};assert(imported.begin(d.context.get(),importKey));
    SpsGameResources resources;SpsGameParameterLayout parameterLayout;parameterLayout.vertex={0,1,2,3,4,5,6,7,8};parameterLayout.pixel={0,1,2,3,4,6};
    std::array<Matrix,40> palette;const Matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};palette.fill(identity);
    const uint32_t white=0xffffffffu,black=0xff000000u;D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width=textureDescription.Height=textureDescription.MipLevels=textureDescription.ArraySize=1;
    textureDescription.Format=DXGI_FORMAT_R8G8B8A8_UNORM;textureDescription.SampleDesc.Count=1;textureDescription.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    textureDescription.Usage=D3D11_USAGE_DEFAULT;D3D11_SUBRESOURCE_DATA textureData{&white,4,4};
    StereoComRef<ID3D11Texture2D> sourceTexture;StereoComRef<ID3D11ShaderResourceView> sourceView;StereoComRef<ID3D11SamplerState> pointSampler;
    assert(SUCCEEDED(d.device->CreateTexture2D(&textureDescription,&textureData,sourceTexture.put())));
    assert(SUCCEEDED(d.device->CreateShaderResourceView(sourceTexture.get(),nullptr,sourceView.put())));
    D3D11_SAMPLER_DESC samplerDescription{};samplerDescription.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDescription.AddressU=samplerDescription.AddressV=samplerDescription.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;samplerDescription.MaxLOD=D3D11_FLOAT32_MAX;
    assert(SUCCEEDED(d.device->CreateSamplerState(&samplerDescription,pointSampler.put())));
    D3D11_RASTERIZER_DESC rasterDescription{};rasterDescription.FillMode=D3D11_FILL_SOLID;rasterDescription.CullMode=D3D11_CULL_NONE;rasterDescription.DepthClipEnable=TRUE;
    StereoComRef<ID3D11RasterizerState> raster;assert(SUCCEEDED(d.device->CreateRasterizerState(&rasterDescription,raster.put())));
    D3D11_DEPTH_STENCIL_DESC depthDescription{};depthDescription.DepthEnable=TRUE;depthDescription.DepthFunc=D3D11_COMPARISON_GREATER;depthDescription.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
    StereoComRef<ID3D11DepthStencilState> opaqueDepth,alphaDepth;assert(SUCCEEDED(d.device->CreateDepthStencilState(&depthDescription,opaqueDepth.put())));
    depthDescription.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;assert(SUCCEEDED(d.device->CreateDepthStencilState(&depthDescription,alphaDepth.put())));
    D3D11_BLEND_DESC blendDescription{};auto& blend=blendDescription.RenderTarget[0];blend.BlendEnable=TRUE;
    blend.SrcBlend=D3D11_BLEND_SRC_ALPHA;blend.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;blend.BlendOp=D3D11_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D11_BLEND_ONE;blend.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;blend.BlendOpAlpha=D3D11_BLEND_OP_ADD;blend.RenderTargetWriteMask=15;
    StereoComRef<ID3D11BlendState> alphaBlend;assert(SUCCEEDED(d.device->CreateBlendState(&blendDescription,alphaBlend.put())));
    SpsScene scene;
    const std::array<float,4> colors[]{{1,0,0,1},{0,1,0,1},{0,0,1,.5f},{1,1,0,.5f}};
    for(unsigned asset=0;asset<4;++asset){SpsGameMeshInputs mesh;mesh.complete=mesh.localTransform=mesh.skinned=true;mesh.mesh=asset+1;
        mesh.vertexBuffer=reinterpret_cast<uintptr_t>(vertices.get());mesh.indexBuffer=reinterpret_cast<uintptr_t>(index.get());mesh.vertexStride=8;
        mesh.vertexShader=reinterpret_cast<uintptr_t>(sourceVs.get());mesh.pixelShader=reinterpret_cast<uintptr_t>(sourcePs.get());mesh.layout=reinterpret_cast<uintptr_t>(sourceLayout.get());
        mesh.topology=asset==1?D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        mesh.indexFormat=DXGI_FORMAT_R32_UINT;mesh.startIndex=asset==1?6:0;mesh.indexCount=asset==1?4:6;
        SpsGameParameters parameters(parameterLayout);const float position[4]{};
        assert(parameters.vertex(4,identity.data(),4)&&parameters.vertex(0,identity.data(),4)&&parameters.vertex(8,position,1));
        assert(parameters.vertex(14,colors[asset].data(),1)&&parameters.vertex(16,identity.data(),4)&&parameters.vertex(84,palette.data(),160));
        SpsGameMaterialInputs material;material.constants=parameters.take();material.raster=raster;material.depth=asset<2?opaqueDepth:alphaDepth;
        material.resources[1].push_back({0,sourceView.get(),1});material.samplers[1].push_back({0,pointSampler});
        if(asset>=2){material.blend=alphaBlend;material.layer=1;material.order=SpsDrawOrder::Transparent;}
        const SpsBounds bounds{{0,0,0},{1,1.25f,0}};
        auto result=import_sps_game_asset(programs,imported,resources,mesh,parameterLayout,std::move(material),bounds,1,1,1,geometry);
        if(!result.complete){std::cerr<<result.reason<<'\n';assert(false);}
        const auto& converted=result.asset.draw.pipeline->vertex;assert(converted.constants.size()==1&&converted.constants[0].sourceSlot==0); // Skinning stays shared.
        assert(scene.set_asset(std::move(result.asset)));
    }
    // The game can change its source texture before submission. Imported SPS
    // assets retain their owned version across every eye and subsequent frame.
    d.context->UpdateSubresource(sourceTexture.get(),0,nullptr,&black,4,4);
    SpsSceneInstance object;object.key=1;object.asset=1;object.world=instance_world(0,2,.25f);assert(scene.set_instance(object));
    object.key=2;object.asset=2;object.world=instance_world(.6f,1,.05f);assert(scene.set_instance(object));
    object.key=3;object.asset=1;object.world=instance_world(100,2,.25f);assert(scene.set_instance(object));
    object.key=4;object.viewMask=0;assert(scene.set_instance(object));object.viewMask=15;
    object.key=5;object.asset=3;object.world=instance_world(-.1f,1.8f,.3f);assert(scene.set_instance(object));
    object.key=6;object.asset=4;object.world=instance_world(.1f,1.8f,.3f);assert(scene.set_instance(object));
    SpsRenderer renderer;SpsCameraPlacement placement;placement.nearClip=.1f;placement.farClip=100;
    SpsRendererDescription description;description.color=format;for(auto& view:description.views)view={64,64};
    for(unsigned frame=1;frame<=2;++frame){QuadFrameInputs input;input.identity={1,frame,3,400+frame,GetCurrentThreadId()};
        for(unsigned view=0;view<4;++view){auto& eye=input.views[view];eye.type=XR_TYPE_VIEW;eye.pose.orientation.w=1;eye.pose.position.x=view%2?.2f:-.2f;
            const float angle=std::atan(view<2?1.f:.5f);eye.fov={-angle,angle,angle,-angle};}
        if(frame==2){object.key=2;object.asset=2;object.world=instance_world(-.6f,1,.05f);assert(scene.set_instance(object));
            palette[0][13]=.25f;SpsGameParameters animation(parameterLayout);assert(animation.vertex(84,palette.data(),160));auto data=animation.take();
            object.key=1;object.asset=1;object.world=instance_world(0,2,.25f);object.parameterVersion=2;
            object.constants[0]=std::make_shared<const StereoConstantBuffers>(std::move(data[0]));assert(scene.set_instance(object));}
        if(!renderer.render(d.context.get(),input,placement,description,scene)){std::cerr<<renderer.reason()<<'\n';assert(false);}
        const auto& stats=renderer.statistics();assert(stats.scene.objectsVisited==6&&stats.scene.objectsPrepared==4);
        assert(stats.scene.viewTransforms==15&&stats.scene.assetsPrepared==4&&stats.scene.bindingSetsPrepared==(frame==1?4:0));
        assert(stats.resourceCopyBytes==0&&stats.resourceSnapshots==0);
        assert(stats.batches[0].pairedDraws==3&&stats.batches[0].monoDraws==2);
        assert(stats.batches[1].pairedDraws==2&&stats.batches[1].monoDraws==3);
        for(const auto& batch:stats.batches){assert(batch.contextCreations==1);assert(batch.stateCalls<60);}
        assert(!renderer.finish(input.identity));assert(!scene.erase_instance(1));
        for(unsigned view=0;view<4;++view){SpsFrameImage image;assert(renderer.image(view,input.identity,image));
            verify_scene(d,image,view,format,frame==2);assert(renderer.consumed(view,input.identity));assert(!renderer.image(view,input.identity,image));}
        assert(renderer.finish(input.identity));assert(!renderer.finish(input.identity));
    }
    const auto changed=resources.shader_view(imported,{0,sourceView.get(),2});assert(changed);
    StereoComRef<ID3D11Resource> changedResource;StereoComRef<ID3D11Texture2D> changedTexture;changed.view()->GetResource(changedResource.put());
    assert(SUCCEEDED(changedResource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(changedTexture.put()))));
    const auto changedPixels=read(d,changedTexture.get(),0,4);assert(changedPixels[0]==0&&changedPixels[1]==0&&changedPixels[2]==0&&changedPixels[3]==255);
    assert(!resources.shader_view(imported,{0,sourceView.get(),1}));
    std::cout<<"SPS-only complete quad scene: imported mesh/material/texture assets, shared skinning and pose updates, independent membership/transparency, analytic four-view pixels/depth, zero frame resource copies and zero native rendering "
        <<(hardware?"hardware":"WARP")<<' '<<(geometry?"GS":"VPRT")<<' '<<(format==DXGI_FORMAT_R16G16B16A16_FLOAT?"HDR":"RGBA8")<<" passed\n";
}
}
int main(){
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
#endif
    for(bool geometry:{false,true})for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT}){
        scene_run(false,geometry,format);if(std::getenv("LMU_QV_STEREO_HARDWARE_TEST"))scene_run(true,geometry,format);
    }
}
