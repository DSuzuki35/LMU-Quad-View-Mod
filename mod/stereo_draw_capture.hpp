#pragma once
#include "stereo_draw.hpp"
#include <d3d11_1.h>

namespace lmuqv {
enum class StereoQueryBoundary {Unestablished,OutsideResultScopes};
struct StereoDrawCaptureRequest {
    StereoFrameIdentity identity;
    uint64_t key=0;
    UINT indexCount=0,instanceCount=1,startIndex=0,startInstance=0;
    INT baseVertex=0;
    std::shared_ptr<const StereoPipeline> pipeline;
    // From the shader/layout creation registry which supplied pipeline's exact
    // bytecode/elements. Compare actual COM identities before any capture.
    ID3D11VertexShader* vertex=nullptr;
    ID3D11PixelShader* pixel=nullptr;
    ID3D11InputLayout* layout=nullptr;
    const std::array<StereoConstantBuffers,2>* constants=nullptr;
    const std::array<std::array<uint64_t,14>,2>* constantTokens=nullptr;
    StereoQueryBoundary queries=StereoQueryBoundary::Unestablished;
};
struct StereoDrawCaptureResult {
    bool complete=false;
    const char* reason="incomplete draw input scope";
    StereoDrawPacket packet;
};

// Complete executable packet from one stable, already-prepared native draw.
// Shaders/layouts must already be converted/cached. All CB bytes come from
// verified CPU shadows; no mapped WRITE memory is read. Only private GPU copies
// are written. This function neither submits nor suppresses a native draw.
inline StereoDrawCaptureResult capture_stereo_draw(ID3D11DeviceContext* context,
    StereoGpuResourcePool& pool,const StereoDrawCaptureRequest& in) noexcept {
    const auto fail=[](const char* reason){StereoDrawCaptureResult out;out.reason=reason;return out;};
    if(!context||!pool.matches(context,in.identity)||!in.pipeline||!in.vertex||!in.pixel||!in.layout||
       !in.constants||!in.constantTokens||in.queries!=StereoQueryBoundary::OutsideResultScopes||!in.indexCount||!in.instanceCount)
        return fail("invalid draw capture scope/query boundary");
    if(!stereo_draw_detail::device_matches(context,in.pipeline->device.get()))return fail("draw capture device differs");
    try {
        StereoComRef<ID3D11VertexShader> vs;StereoComRef<ID3D11PixelShader> ps;
        StereoComRef<ID3D11GeometryShader> gs;StereoComRef<ID3D11HullShader> hs;StereoComRef<ID3D11DomainShader> ds;
        UINT classes=0;context->VSGetShader(vs.put(),nullptr,&classes);if(classes)return fail("native vertex class instances");
        classes=0;context->PSGetShader(ps.put(),nullptr,&classes);if(classes)return fail("native pixel class instances");
        context->GSGetShader(gs.put(),nullptr,nullptr);context->HSGetShader(hs.put(),nullptr,nullptr);context->DSGetShader(ds.put(),nullptr,nullptr);
        StereoComRef<ID3D11InputLayout> layout;context->IAGetInputLayout(layout.put());
        if(vs.get()!=in.vertex||ps.get()!=in.pixel||layout.get()!=in.layout||gs||hs||ds)return fail("native shader/layout binding differs");
        D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
        if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)return fail("unsupported native draw topology");
        StereoComRef<ID3D11Predicate> predicate;BOOL predicateValue=FALSE;context->GetPredication(predicate.put(),&predicateValue);
        if(predicate)return fail("native predication active");
        std::array<ID3D11Buffer*,4> stream{};context->SOGetTargets(UINT(stream.size()),stream.data());bool hasStream=false;
        for(auto* object:stream)if(object){hasStream=true;object->Release();}if(hasStream)return fail("native stream output active");
        const UINT uavCount=in.pipeline->device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64:8;
        std::array<ID3D11UnorderedAccessView*,64> uavs{};
        context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,uavCount,uavs.data());
        bool hasUav=false;for(auto* uav:uavs)if(uav){hasUav=true;uav->Release();}
        if(hasUav)return fail("native graphics UAV active");
        // Exact one-color/depth output is the supported batch contract.
        std::array<ID3D11RenderTargetView*,8> targets{};StereoComRef<ID3D11DepthStencilView> depthTarget;
        context->OMGetRenderTargets(UINT(targets.size()),targets.data(),depthTarget.put());
        bool output=targets[0]&&depthTarget;for(unsigned i=0;i<targets.size();++i){if(i&&targets[i])output=false;if(targets[i])targets[i]->Release();}
        if(!output)return fail("native draw requires unsupported outputs");
        StereoDrawPacket packet;packet.key=in.key;packet.pipeline=in.pipeline;
        packet.indexCount=in.indexCount;packet.instanceCount=in.instanceCount;packet.startIndex=in.startIndex;
        packet.startInstance=in.startInstance;packet.baseVertex=in.baseVertex;
        context->OMGetBlendState(packet.blend.put(),packet.blendFactor.data(),&packet.sampleMask);
        context->OMGetDepthStencilState(packet.depth.put(),&packet.stencilRef);context->RSGetState(packet.raster.put());
        UINT count=0;context->RSGetViewports(&count,nullptr);if(count!=1)return fail("native draw viewport count");
        context->RSGetViewports(&count,&packet.viewport);
        count=0;context->RSGetScissorRects(&count,nullptr);if(count>1)return fail("native draw scissor count");
        if(count)context->RSGetScissorRects(&count,&packet.scissor);
        else if(packet.raster){D3D11_RASTERIZER_DESC raster{};packet.raster->GetDesc(&raster);if(raster.ScissorEnable)return fail("missing native scissor");}
        StereoComRef<ID3D11DeviceContext1> context1;
        context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(context1.put()));
        // Validate all CPU-shadow bindings before allocating resource snapshots.
        for(unsigned stage=0;stage<2;++stage){const auto& shader=stage?in.pipeline->pixel:in.pipeline->vertex;
            for(unsigned slot=0;slot<14;++slot)if(shader.constantRegisters[slot]){
                StereoComRef<ID3D11Buffer> actual;UINT first=0,registers=4096;
                if(context1){if(stage)context1->PSGetConstantBuffers1(slot,1,actual.put(),&first,&registers);
                    else context1->VSGetConstantBuffers1(slot,1,actual.put(),&first,&registers);}
                else if(stage)context->PSGetConstantBuffers(slot,1,actual.put());else context->VSGetConstantBuffers(slot,1,actual.put());
                const auto& bytes=(*in.constants)[stage][slot];
                if(!actual||reinterpret_cast<uintptr_t>(actual.get())!=(*in.constantTokens)[stage][slot]||first||
                   registers<shader.constantRegisters[slot]||bytes.size()<size_t(shader.constantRegisters[slot])*16||bytes.size()>65536||bytes.size()%16)
                    return fail("native CPU shadow/bound constant range differs");
                packet.stages[stage].constants[slot]=bytes;
            }
        }
        std::unordered_map<ID3D11Resource*,StereoResourceLease> frozen;
        const auto freeze=[&](ID3D11Resource* resource){const auto found=frozen.find(resource);
            if(found!=frozen.end())return found->second;
            auto lease=pool.freeze(resource);if(lease)frozen.emplace(resource,lease);return lease;};
        StereoComRef<ID3D11Buffer> indices;context->IAGetIndexBuffer(indices.put(),&packet.indexFormat,&packet.indexOffset);
        if(!indices)return fail("missing native index buffer");packet.indices=freeze(indices.get());if(!packet.indices)return fail(pool.reason());
        for(UINT slot=0;slot<32;++slot)if(in.pipeline->inputSlots&(1u<<slot)){
            StereoComRef<ID3D11Buffer> vertex;UINT stride=0,offset=0;context->IAGetVertexBuffers(slot,1,vertex.put(),&stride,&offset);
            if(!vertex)return fail("missing native vertex buffer");auto lease=freeze(vertex.get());if(!lease)return fail(pool.reason());
            packet.vertices.push_back({slot,stride,offset,lease});
        }
        for(unsigned stage=0;stage<2;++stage){const auto& shader=stage?in.pipeline->pixel:in.pipeline->vertex;
            for(UINT slot=0;slot<128;++slot)if(in.pipeline->resourceSlots[stage][slot/32]&(1u<<(slot%32))){
                StereoComRef<ID3D11ShaderResourceView> view;if(stage)context->PSGetShaderResources(slot,1,view.put());else context->VSGetShaderResources(slot,1,view.put());
                StereoShaderResourceLease binding;
                if(view){StereoComRef<ID3D11Resource> resource;view->GetResource(resource.put());const auto lease=freeze(resource.get());if(!lease)return fail(pool.reason());
                    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};view->GetDesc(&desc);binding=pool.create_shader_view(lease,desc);if(!binding)return fail(pool.reason());}
                packet.stages[stage].resources.push_back({slot,binding});
            }
            for(UINT slot=0;slot<16;++slot)if(shader.samplerSlots&(1u<<slot)){
                StereoComRef<ID3D11SamplerState> sampler;if(stage)context->PSGetSamplers(slot,1,sampler.put());else context->VSGetSamplers(slot,1,sampler.put());
                packet.stages[stage].samplers.push_back({slot,std::move(sampler)});
            }
        }
        if(!stereo_draw_detail::valid(packet,in.identity,in.pipeline->device.get()))return fail("captured native draw packet invalid");
        StereoDrawCaptureResult out;out.complete=true;out.packet=std::move(packet);out.reason="complete owned native draw inputs";return out;
    }catch(...){return fail("native draw input allocation failure");}
}
}
