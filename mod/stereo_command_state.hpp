#pragma once
#include "stereo_draw.hpp"
#include <d3d11_1.h>

namespace lmuqv {
// State of one freshly cleared owned recording context. Only changed ranges
// reach the driver; shared mesh/material bindings survive adjacent SPS draws.
// No Get* calls, reflection, native state inspection or COM retention per draw.
class StereoCommandState {
    ID3D11DeviceContext* context_;
    ID3D11DeviceContext1* context1_;
    bool offsets_,emulated_,first_=true;
    size_t calls_=0;
    const StereoDrawPacket* previous_=nullptr;
    ID3D11RenderTargetView* color_=nullptr;
    ID3D11DepthStencilView* depth_=nullptr;
    ID3D11InputLayout* layout_=nullptr;
    ID3D11VertexShader* vertex_=nullptr;
    ID3D11PixelShader* pixel_=nullptr;
    ID3D11GeometryShader* geometry_=nullptr;
    std::array<ID3D11Buffer*,32> vertices_{};
    std::array<UINT,32> strides_{},offsetsVertex_{};
    std::array<std::array<ID3D11Buffer*,14>,2> constants_{};
    std::array<std::array<UINT,14>,2> constantFirst_{},constantCount_{};
    std::array<std::array<ID3D11ShaderResourceView*,128>,2> resources_{};
    std::array<std::array<ID3D11SamplerState*,16>,2> samplers_{};
    template<class T,size_t N,class Bind>void changed(std::array<T,N>& current,const std::array<T,N>& desired,Bind bind){
        for(UINT i=0;i<N;){if(current[i]==desired[i]){++i;continue;}const UINT begin=i;
            do{current[i]=desired[i];++i;}while(i<N&&current[i]!=desired[i]);
            bind(begin,i-begin,desired.data()+begin);++calls_;}
    }
public:
    StereoCommandState(ID3D11DeviceContext* context,ID3D11DeviceContext1* context1,bool offsets,bool emulated)
        :context_(context),context1_(context1),offsets_(offsets),emulated_(emulated){}
    void draw(const StereoDrawPacket& d,bool paired,ID3D11RenderTargetView* color,ID3D11DepthStencilView* depth){
        if(color!=color_||depth!=depth_){context_->OMSetRenderTargets(1,&color,depth);color_=color;depth_=depth;++calls_;}
        if(first_||d.blend.get()!=previous_->blend.get()||d.sampleMask!=previous_->sampleMask||d.blendFactor!=previous_->blendFactor){
            context_->OMSetBlendState(d.blend.get(),d.blendFactor.data(),d.sampleMask);++calls_;}
        if(first_||d.depth.get()!=previous_->depth.get()||d.stencilRef!=previous_->stencilRef){context_->OMSetDepthStencilState(d.depth.get(),d.stencilRef);++calls_;}
        if(first_||d.raster.get()!=previous_->raster.get()){context_->RSSetState(d.raster.get());++calls_;}
        if(first_||std::memcmp(&d.viewport,&previous_->viewport,sizeof(d.viewport))){context_->RSSetViewports(1,&d.viewport);++calls_;}
        if(first_||std::memcmp(&d.scissor,&previous_->scissor,sizeof(d.scissor))){context_->RSSetScissorRects(1,&d.scissor);++calls_;}
        std::array<ID3D11Buffer*,32> vertices{};std::array<UINT,32> strides{},offsets{};
        for(const auto& v:d.vertices){vertices[v.slot]=static_cast<ID3D11Buffer*>(v.buffer.resource());strides[v.slot]=v.stride;offsets[v.slot]=v.offset;}
        for(UINT i=0;i<32;){if(vertices_[i]==vertices[i]&&strides_[i]==strides[i]&&offsetsVertex_[i]==offsets[i]){++i;continue;}const UINT begin=i;
            do{vertices_[i]=vertices[i];strides_[i]=strides[i];offsetsVertex_[i]=offsets[i];++i;}
            while(i<32&&(vertices_[i]!=vertices[i]||strides_[i]!=strides[i]||offsetsVertex_[i]!=offsets[i]));
            context_->IASetVertexBuffers(begin,i-begin,vertices.data()+begin,strides.data()+begin,offsets.data()+begin);++calls_;}
        if(first_||!d.indices.same_contents(previous_->indices)||d.indexFormat!=previous_->indexFormat||d.indexOffset!=previous_->indexOffset){
            context_->IASetIndexBuffer(static_cast<ID3D11Buffer*>(d.indices.resource()),d.indexFormat,d.indexOffset);++calls_;}
        if(first_||d.topology!=previous_->topology){context_->IASetPrimitiveTopology(d.topology);++calls_;}
        auto* layout=paired?d.pipeline->pairedLayout.get():d.pipeline->monoLayout.get();
        auto* vertex=paired?d.pipeline->pairedVs.get():d.pipeline->monoVs.get();
        auto* pixel=paired?d.pipeline->pairedPs.get():d.pipeline->monoPs.get();
        auto* geometry=paired?d.pipeline->geometry.get():nullptr;
        if(layout!=layout_){context_->IASetInputLayout(layout);layout_=layout;++calls_;}
        if(vertex!=vertex_){context_->VSSetShader(vertex,nullptr,0);vertex_=vertex;++calls_;}
        if(pixel!=pixel_){context_->PSSetShader(pixel,nullptr,0);pixel_=pixel;++calls_;}
        if(geometry!=geometry_){context_->GSSetShader(geometry,nullptr,0);geometry_=geometry;++calls_;}
        for(unsigned stage=0;stage<2;++stage){
            std::array<ID3D11ShaderResourceView*,128> resources{};std::array<ID3D11SamplerState*,16> samplers{};
            for(const auto& r:d.stages[stage].resources)resources[r.slot]=r.resource.view();
            for(const auto& s:d.stages[stage].samplers)samplers[s.slot]=s.state.get();
            changed(resources_[stage],resources,[&](UINT first,UINT count,ID3D11ShaderResourceView* const* values){
                if(stage)context_->PSSetShaderResources(first,count,values);else context_->VSSetShaderResources(first,count,values);});
            changed(samplers_[stage],samplers,[&](UINT first,UINT count,ID3D11SamplerState* const* values){
                if(stage)context_->PSSetSamplers(first,count,values);else context_->VSSetSamplers(first,count,values);});
        }
        first_=false;previous_=&d;
    }
    void constants(unsigned stage,const std::array<ID3D11Buffer*,14>& buffers,const std::array<UINT,14>& first,const std::array<UINT,14>& count){
        auto& old=constants_[stage];auto& oldFirst=constantFirst_[stage];auto& oldCount=constantCount_[stage];
        for(UINT i=0;i<14;){if(old[i]==buffers[i]&&(!offsets_||(oldFirst[i]==first[i]&&oldCount[i]==count[i]))){++i;continue;}const UINT begin=i;
            do{old[i]=buffers[i];oldFirst[i]=first[i];oldCount[i]=count[i];++i;}
            while(i<14&&(old[i]!=buffers[i]||(offsets_&&(oldFirst[i]!=first[i]||oldCount[i]!=count[i]))));
            if(!offsets_){if(stage)context_->PSSetConstantBuffers(begin,i-begin,buffers.data()+begin);
                else context_->VSSetConstantBuffers(begin,i-begin,buffers.data()+begin);++calls_;continue;}
            UINT from=begin,to=i;
            if(emulated_){const std::array<ID3D11Buffer*,14> empty{};
                if(stage)context_->PSSetConstantBuffers(begin,i-begin,empty.data());else context_->VSSetConstantBuffers(begin,i-begin,empty.data());++calls_;
                // D3D11 command-list emulation requires rebinding both non-null
                // edges after an offset-only change. Null the changed range,
                // then omit its null edges from SetConstantBuffers1.
                while(from<to&&!buffers[from])++from;while(to>from&&!buffers[to-1])--to;}
            if(to>from){if(stage)context1_->PSSetConstantBuffers1(from,to-from,buffers.data()+from,first.data()+from,count.data()+from);
                else context1_->VSSetConstantBuffers1(from,to-from,buffers.data()+from,first.data()+from,count.data()+from);++calls_;}
        }
    }
    size_t calls()const{return calls_;}
};
}
