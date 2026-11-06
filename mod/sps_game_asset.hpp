#pragma once
#include "sps_game_parameters.hpp"
#include "stereo_native_registry.hpp"

namespace lmuqv {
struct SpsGameResourceInput {unsigned slot=0;ID3D11ShaderResourceView* view=nullptr;uint64_t version=0;};
struct SpsGameMaterialInputs {
    std::array<StereoConstantBuffers,2> constants;
    std::array<std::vector<SpsGameResourceInput>,2> resources;
    std::array<std::vector<StereoSamplerBinding>,2> samplers;
    StereoComRef<ID3D11BlendState> blend;
    StereoComRef<ID3D11DepthStencilState> depth;
    StereoComRef<ID3D11RasterizerState> raster;
    std::array<float,4> blendFactor{};
    UINT sampleMask=0xffffffffu,stencilRef=0;
    unsigned layer=0;
    SpsDrawOrder order=SpsDrawOrder::Opaque;
};
// Versions come from the owning game-data producer, never from eye-draw order.
// One snapshot per changed resource; unchanged assets incur no driver queries,
// GPU copies or view creation when the next frame admits another eye.
class SpsGameResources {
    struct Snapshot {uint64_t version=0;StereoResourceLease contents;};
    struct View {uint64_t version=0;StereoShaderResourceLease binding;StereoComRef<ID3D11ShaderResourceView> source;};
    std::unordered_map<ID3D11Resource*,Snapshot> resources_;
    std::unordered_map<ID3D11ShaderResourceView*,View> views_;
    const char* reason_="SPS game asset resources empty";
public:
    StereoResourceLease resource(StereoGpuResourcePool& pool,ID3D11Resource* source,uint64_t version){
        if(!source||!version){reason_="missing SPS resource version";return {};}
        const auto found=resources_.find(source);
        if(found!=resources_.end()){
            if(version<found->second.version){reason_="stale SPS resource version";return {};}
            if(version==found->second.version){auto lease=pool.pin(found->second.contents);if(!lease)reason_=pool.reason();return lease;}
        }
        auto contents=pool.freeze(source);if(!contents){reason_=pool.reason();return {};}
        resources_[source]={version,contents};return contents;
    }
    StereoShaderResourceLease shader_view(StereoGpuResourcePool& pool,const SpsGameResourceInput& input){
        if(!input.view)return {};if(!input.version){reason_="missing SPS texture version";return {};}
        const auto found=views_.find(input.view);
        if(found!=views_.end()){
            if(input.version<found->second.version){reason_="stale SPS texture view version";return {};}
            if(input.version==found->second.version){auto binding=pool.pin(found->second.binding);if(!binding)reason_=pool.reason();return binding;}
        }
        StereoComRef<ID3D11Resource> source;input.view->GetResource(source.put());const auto contents=resource(pool,source.get(),input.version);
        if(!contents)return {};D3D11_SHADER_RESOURCE_VIEW_DESC description{};input.view->GetDesc(&description);
        auto binding=pool.create_shader_view(contents,description);if(!binding){reason_=pool.reason();return {};}
        views_[input.view]={input.version,binding,StereoComRef<ID3D11ShaderResourceView>(input.view)};return binding;
    }
    void clear(){views_.clear();resources_.clear();}
    const char* reason()const{return reason_;}
};
struct SpsGameAssetResult {
    bool complete=false;
    const char* reason="incomplete SPS game asset";
    SpsSceneAsset asset;
};

inline SpsGameAssetResult import_sps_game_asset(StereoNativeRegistry& programs,StereoGpuResourcePool& pool,SpsGameResources& resources,
    const SpsGameMeshInputs& mesh,const SpsGameParameterLayout& layout,SpsGameMaterialInputs material,const SpsBounds& bounds,
    uint64_t assetVersion,uint64_t vertexVersion,uint64_t indexVersion,bool forceGeometry=false) noexcept {
    const auto fail=[](const char* why){SpsGameAssetResult out;out.reason=why;return out;};
    if(!mesh.complete||!mesh.mesh||!layout.valid()||!assetVersion||!vertexVersion||!indexVersion)return fail("invalid SPS game asset scope");
    try{
        const StereoNativePipelineKey key{reinterpret_cast<ID3D11VertexShader*>(uintptr_t(mesh.vertexShader)),
            reinterpret_cast<ID3D11PixelShader*>(uintptr_t(mesh.pixelShader)),reinterpret_cast<ID3D11InputLayout*>(uintptr_t(mesh.layout))};
        // Shader/layout identities must have been observed at resource creation;
        // reject unknown metadata before following any geometry COM token.
        auto* device=pool.device();if(!device)return fail("SPS game asset resource frame missing");
        const uint32_t vertexView=1u<<layout.vertex[0],pixelView=(1u<<layout.pixel[0])|(1u<<5);
        const auto pipeline=programs.resolve_asset(device,key,vertexView,pixelView,forceGeometry);
        if(!pipeline)return fail("SPS game shader creation metadata/conversion unavailable");
        if(pipeline->inputSlots!=1)return fail("SPS game vertex streams incomplete");
        for(unsigned stage=0;stage<2;++stage){const auto& shader=stage?pipeline->pixel:pipeline->vertex;
            for(unsigned slot=0;slot<14;++slot)if(!shader.constantRegisters[slot])material.constants[stage][slot].clear();}
        SpsGameAssetResult out;auto& asset=out.asset;auto& draw=asset.draw;
        asset.key=mesh.mesh;asset.version=assetVersion;asset.bounds=bounds;asset.layer=material.layer;asset.order=material.order;
        asset.verticesInWorldSpace=!mesh.localTransform;asset.patches=sps_game_transform_patches(layout,material.constants);
        draw.key=mesh.mesh;draw.pipeline=pipeline;draw.topology=mesh.topology;draw.indexFormat=mesh.indexFormat;
        draw.indexCount=mesh.indexCount;draw.startIndex=mesh.startIndex;draw.baseVertex=mesh.baseVertex;
        draw.indices=resources.resource(pool,reinterpret_cast<ID3D11Buffer*>(uintptr_t(mesh.indexBuffer)),indexVersion);
        const auto vertices=resources.resource(pool,reinterpret_cast<ID3D11Buffer*>(uintptr_t(mesh.vertexBuffer)),vertexVersion);
        if(!draw.indices||!vertices)return fail(resources.reason());draw.vertices.push_back({0,mesh.vertexStride,0,vertices});
        draw.blend=std::move(material.blend);draw.depth=std::move(material.depth);draw.raster=std::move(material.raster);
        draw.blendFactor=material.blendFactor;draw.sampleMask=material.sampleMask;draw.stencilRef=material.stencilRef;
        for(unsigned stage=0;stage<2;++stage){auto& bindings=draw.stages[stage];const auto& shader=stage?pipeline->pixel:pipeline->vertex;
            bindings.constants=std::move(material.constants[stage]);
            for(auto& sampler:material.samplers[stage]){if(sampler.slot>=16)return fail("invalid SPS material sampler slot");
                if(shader.samplerSlots&(1u<<sampler.slot))bindings.samplers.push_back(std::move(sampler));}
            for(const auto& source:material.resources[stage]){if(source.slot>=128)return fail("invalid SPS material resource slot");
                if(!(pipeline->resourceSlots[stage][source.slot/32]&(1u<<(source.slot%32))))continue;
                auto binding=resources.shader_view(pool,source);
                if(source.view&&!binding)return fail(resources.reason());bindings.resources.push_back({source.slot,std::move(binding)});}}
        // Use a temporary nonempty viewport only for complete packet validation;
        // the renderer sets each view's actual runtime extent before submission.
        draw.viewport={0,0,1,1,0,1};draw.scissor={0,0,1,1};draw.bindingDevices=StereoBindingDevices::prepare(draw);
        if(!draw.bindingDevices||!stereo_draw_detail::valid(draw,draw.indices.identity(),device))return fail("incomplete SPS game material/geometry bindings");
        out.complete=true;out.reason="complete owned SPS game asset; no native rendering/preparation";return out;
    }catch(...){return fail("SPS game asset allocation/reader failure");}
}
}
