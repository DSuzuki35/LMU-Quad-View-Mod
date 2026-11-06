#pragma once
#include "stereo_pipeline.hpp"
#include "stereo_gpu_resources.hpp"
#include "stereo_constants.hpp"
#include <cmath>

namespace lmuqv {
struct StereoVertexBinding {UINT slot=0,stride=0,offset=0;StereoResourceLease buffer;};
struct StereoResourceBinding {UINT slot=0;StereoShaderResourceLease resource;};
struct StereoSamplerBinding {UINT slot=0;StereoComRef<ID3D11SamplerState> state;};
class StereoBindingDevices;
struct StereoStageBindings {
    StereoConstantBuffers constants;
    std::shared_ptr<const StereoConstantBuffers> sharedConstants;
    std::shared_ptr<const StereoConstantBuffers> objectConstants;
    const std::vector<uint8_t>& constant(unsigned slot)const{
        if(!constants[slot].empty())return constants[slot];
        if(objectConstants&&!(*objectConstants)[slot].empty())return (*objectConstants)[slot];
        return sharedConstants?(*sharedConstants)[slot]:constants[slot];
    }
    // Sorted, unique slots. Even explicitly null bindings occupy a slot.
    std::vector<StereoResourceBinding> resources;
    std::vector<StereoSamplerBinding> samplers;
};
struct StereoDrawPacket {
    uint64_t key=0;
    std::shared_ptr<const StereoPipeline> pipeline;
    std::vector<StereoVertexBinding> vertices;
    StereoResourceLease indices;
    DXGI_FORMAT indexFormat=DXGI_FORMAT_R32_UINT;
    D3D11_PRIMITIVE_TOPOLOGY topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    UINT indexOffset=0,indexCount=0,instanceCount=1,startIndex=0,startInstance=0;
    INT baseVertex=0;
    std::array<StereoStageBindings,2> stages;
    std::shared_ptr<const StereoBindingDevices> bindingDevices;
    StereoComRef<ID3D11BlendState> blend;
    StereoComRef<ID3D11DepthStencilState> depth;
    StereoComRef<ID3D11RasterizerState> raster;
    std::array<float,4> blendFactor{};
    UINT sampleMask=0xffffffffu,stencilRef=0;
    D3D11_VIEWPORT viewport{};
    D3D11_RECT scissor{};
};
// D3D state objects are immutable. Validate their devices at asset preparation,
// then match the retained bindings by identity instead of querying the driver
// again for every object in every view. A changed binding invalidates the seal.
class StereoBindingDevices {
    ID3D11Device* device_=nullptr;
    ID3D11BlendState* blend_=nullptr;
    ID3D11DepthStencilState* depth_=nullptr;
    ID3D11RasterizerState* raster_=nullptr;
    std::array<std::vector<std::pair<UINT,ID3D11SamplerState*>>,2> samplers_;
    StereoBindingDevices()=default;
    template<class T>static bool device(T* object,ID3D11Device* expected){
        if(!object)return true;StereoComRef<ID3D11Device> actual;object->GetDevice(actual.put());return actual.get()==expected;
    }
public:
    static std::shared_ptr<const StereoBindingDevices> prepare(const StereoDrawPacket& draw){
        if(!draw.pipeline||!draw.pipeline->device)return {};
        auto result=std::shared_ptr<StereoBindingDevices>(new StereoBindingDevices);result->device_=draw.pipeline->device.get();
        if(!device(draw.blend.get(),result->device_)||!device(draw.depth.get(),result->device_)||!device(draw.raster.get(),result->device_))return {};
        result->blend_=draw.blend.get();result->depth_=draw.depth.get();result->raster_=draw.raster.get();
        for(unsigned stage=0;stage<2;++stage)for(const auto& sampler:draw.stages[stage].samplers){
            if(!device(sampler.state.get(),result->device_))return {};result->samplers_[stage].push_back({sampler.slot,sampler.state.get()});}
        return result;
    }
    bool matches(const StereoDrawPacket& draw,ID3D11Device* device)const{
        if(device!=device_||draw.blend.get()!=blend_||draw.depth.get()!=depth_||draw.raster.get()!=raster_)return false;
        for(unsigned stage=0;stage<2;++stage){const auto& expected=samplers_[stage];const auto& actual=draw.stages[stage].samplers;
            if(expected.size()!=actual.size())return false;
            for(size_t i=0;i<expected.size();++i)if(expected[i].first!=actual[i].slot||expected[i].second!=actual[i].state.get())return false;}
        return true;
    }
};
namespace stereo_draw_detail {
template<class T> inline bool device_matches(T* object,ID3D11Device* device){
    if(!object)return true;StereoComRef<ID3D11Device> actual;object->GetDevice(actual.put());return actual.get()==device;
}
inline bool buffer_valid(const StereoResourceLease& lease,const StereoFrameIdentity& key,ID3D11Device* device,UINT bind,UINT offset,UINT minimum=0){
    if(!lease||lease.identity()!=key||lease.resource_device()!=device)return false;
    const auto* desc=lease.buffer_description();return desc&&(desc->BindFlags&bind)&&offset<desc->ByteWidth&&minimum<=desc->ByteWidth-offset;
}
inline bool valid(const StereoDrawPacket& d,const StereoFrameIdentity& key,ID3D11Device* device){
    if(!d.pipeline||d.pipeline->device.get()!=device||!d.indexCount||!d.instanceCount||d.vertices.size()>32||
       (d.topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST&&d.topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)||
       (d.indexFormat!=DXGI_FORMAT_R16_UINT&&d.indexFormat!=DXGI_FORMAT_R32_UINT)||
       uint64_t(d.instanceCount)+d.startInstance>(uint64_t(1)<<32))return false;
    const UINT indexBytes=d.indexFormat==DXGI_FORMAT_R16_UINT?2:4;
    const uint64_t required=(uint64_t(d.startIndex)+d.indexCount)*indexBytes;
    if(d.indexOffset%indexBytes||required>0xffffffffu||!buffer_valid(d.indices,key,device,D3D11_BIND_INDEX_BUFFER,d.indexOffset,UINT(required)))return false;
    uint32_t slots=0;UINT previous=0;bool first=true;
    for(const auto& v:d.vertices){if(v.slot>=32||(!first&&v.slot<=previous)||!v.stride||v.stride>2048||!buffer_valid(v.buffer,key,device,D3D11_BIND_VERTEX_BUFFER,v.offset,v.stride))return false;
        slots|=1u<<v.slot;previous=v.slot;first=false;}
    if((slots&d.pipeline->inputSlots)!=d.pipeline->inputSlots)return false;
    const auto& v=d.viewport;
    if(!std::isfinite(v.TopLeftX)||!std::isfinite(v.TopLeftY)||!std::isfinite(v.Width)||!std::isfinite(v.Height)||
       !std::isfinite(v.MinDepth)||!std::isfinite(v.MaxDepth)||v.Width<=0||v.Height<=0||v.Width>32767||v.Height>32767||
       v.TopLeftX< -32768||v.TopLeftY< -32768||v.TopLeftX+v.Width>32767||v.TopLeftY+v.Height>32767||
       v.MinDepth<0||v.MaxDepth>1||v.MinDepth>v.MaxDepth||d.scissor.left>d.scissor.right||d.scissor.top>d.scissor.bottom)return false;
    for(float f:d.blendFactor)if(!std::isfinite(f))return false;
    const bool verified=d.bindingDevices&&d.bindingDevices->matches(d,device);
    if(!verified&&(!device_matches(d.blend.get(),device)||!device_matches(d.depth.get(),device)||!device_matches(d.raster.get(),device)))return false;
    for(unsigned stage=0;stage<2;++stage){const auto& b=d.stages[stage];const auto& shader=stage?d.pipeline->pixel:d.pipeline->vertex;
        for(unsigned slot=0;slot<14;++slot)if(shader.constantRegisters[slot]){
            const auto& bytes=b.constant(slot);if(bytes.size()<size_t(shader.constantRegisters[slot])*16||bytes.size()>65536||bytes.size()%16)return false;}
        if(b.resources.size()>128||b.samplers.size()>16)return false;
        std::array<uint32_t,4> resourceMask{};first=true;
        for(const auto& r:b.resources){if(r.slot>=128||(!first&&r.slot<=previous)||
            (r.resource&&(r.resource.identity()!=key||r.resource.resource_device()!=device)))return false;
            resourceMask[r.slot/32]|=1u<<(r.slot%32);previous=r.slot;first=false;}
        for(unsigned i=0;i<4;++i)if((resourceMask[i]&d.pipeline->resourceSlots[stage][i])!=d.pipeline->resourceSlots[stage][i])return false;
        uint32_t samplerMask=0;first=true;
        for(const auto& s:b.samplers){if(s.slot>=16||(!first&&s.slot<=previous)||(!verified&&!device_matches(s.state.get(),device)))return false;
            samplerMask|=1u<<s.slot;previous=s.slot;first=false;}
        if((samplerMask&shader.samplerSlots)!=shader.samplerSlots)return false;
    }
    return true;
}
inline bool pairable(const StereoDrawPacket& a,const StereoDrawPacket& b){
    if(a.pipeline!=b.pipeline||a.topology!=b.topology||a.indexFormat!=b.indexFormat||a.indexOffset!=b.indexOffset||a.indexCount!=b.indexCount||
       a.instanceCount!=b.instanceCount||a.startIndex!=b.startIndex||a.startInstance!=b.startInstance||a.baseVertex!=b.baseVertex||
       !a.indices.same_contents(b.indices)||a.vertices.size()!=b.vertices.size()||a.blend.get()!=b.blend.get()||a.depth.get()!=b.depth.get()||
       a.raster.get()!=b.raster.get()||a.sampleMask!=b.sampleMask||a.stencilRef!=b.stencilRef||
       std::memcmp(a.blendFactor.data(),b.blendFactor.data(),16)||std::memcmp(&a.viewport,&b.viewport,sizeof(a.viewport))||
       std::memcmp(&a.scissor,&b.scissor,sizeof(a.scissor)))return false;
    StereoInstances instances;if(!stereo_instances(a.instanceCount,a.startInstance,instances))return false;
    for(size_t i=0;i<a.vertices.size();++i){const auto& x=a.vertices[i];const auto& y=b.vertices[i];
        if(x.slot!=y.slot||x.stride!=y.stride||x.offset!=y.offset||!x.buffer.same_contents(y.buffer))return false;}
    for(unsigned stage=0;stage<2;++stage){const auto& x=a.stages[stage];const auto& y=b.stages[stage];const auto& shader=stage?a.pipeline->pixel:a.pipeline->vertex;
        if(x.resources.size()!=y.resources.size()||x.samplers.size()!=y.samplers.size())return false;
        for(size_t i=0;i<x.resources.size();++i){const auto& r=x.resources[i];const auto& s=y.resources[i];
            if(r.slot!=s.slot||bool(r.resource)!=bool(s.resource)||(r.resource&&!r.resource.same_binding(s.resource)))return false;}
        for(size_t i=0;i<x.samplers.size();++i)if(x.samplers[i].slot!=y.samplers[i].slot||x.samplers[i].state.get()!=y.samplers[i].state.get())return false;
        uint32_t packed=0;for(const auto& range:shader.constants)packed|=1u<<range.sourceSlot;
        for(unsigned slot=0;slot<14;++slot)if(shader.constantRegisters[slot]&&!(packed&(1u<<slot))&&
            x.constant(slot).data()!=y.constant(slot).data()&&
            std::memcmp(x.constant(slot).data(),y.constant(slot).data(),size_t(shader.constantRegisters[slot])*16))return false;
    }
    return true;
}
}
}
