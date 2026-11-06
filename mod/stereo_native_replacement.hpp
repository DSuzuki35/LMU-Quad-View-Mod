#pragma once
#include "stereo_opaque_layer.hpp"
#include "stereo_batch.hpp"
#include <atomic>
#include <mutex>
#include <unordered_set>

namespace lmuqv {
struct StereoNativeReplacementCounts {
    uint64_t pairedSubmissions=0,leftDrawsRemoved=0,rightDrawsRemoved=0,rightFallbacks=0;
};
// A bounded first native replacement: submit one prepared left draw and an
// early right prediction together. Left uses actual captured inputs. Right is
// consumed only when its ORIGINAL preparation reaches the same draw and all
// resulting inputs agree. Visibility and every native preparation side effect
// remain in their original order. A refused/missing right draw stays native.
// The D3D adapter must report all writes/unknown command lists between snapshots
// and comparison. This component never treats a COM pointer as a content version.
class StereoNativeReplacement {
    enum class Phase {Empty,PendingRight,Finished,Fallback};
    Phase phase_=Phase::Empty;
    StereoFrameIdentity identity_;
    StereoComRef<ID3D11DeviceContext> context_;
    StereoOpaqueLayerTargets targets_;
    StereoBatch batch_;
    std::array<std::vector<StereoDrawPacket>,2> packets_;
    std::mutex resourceMutex_;
    std::unordered_set<ID3D11Resource*> sources_;
    std::atomic<bool> watching_{false},changed_{false};
    StereoNativeReplacementCounts counts_;
    const char* reason_="no native replacement";
    static bool same_resources(const StereoDrawPacket& a,const StereoDrawPacket& b){
        if(a.indices.source_resource()!=b.indices.source_resource()||a.vertices.size()!=b.vertices.size())return false;
        for(size_t i=0;i<a.vertices.size();++i){const auto& x=a.vertices[i];const auto& y=b.vertices[i];
            if(x.slot!=y.slot||x.stride!=y.stride||x.offset!=y.offset||x.buffer.source_resource()!=y.buffer.source_resource())return false;}
        for(unsigned stage=0;stage<2;++stage){const auto& x=a.stages[stage];const auto& y=b.stages[stage];
            if(x.resources.size()!=y.resources.size()||x.samplers.size()!=y.samplers.size())return false;
            for(size_t i=0;i<x.resources.size();++i){const auto& p=x.resources[i];const auto& q=y.resources[i];
                if(p.slot!=q.slot||bool(p.resource)!=bool(q.resource))return false;
                if(p.resource){
                    if(p.resource.source_resource()!=q.resource.source_resource())return false;
                    D3D11_SHADER_RESOURCE_VIEW_DESC pd{},qd{};p.resource.view()->GetDesc(&pd);q.resource.view()->GetDesc(&qd);
                    if(std::memcmp(&pd,&qd,sizeof(pd)))return false;
                }
            }
            for(size_t i=0;i<x.samplers.size();++i)if(x.samplers[i].slot!=y.samplers[i].slot||x.samplers[i].state.get()!=y.samplers[i].state.get())return false;
        }
        return true;
    }
    static bool same_draw(const StereoDrawPacket& a,const StereoDrawPacket& b){
        if(a.pipeline!=b.pipeline||a.key!=b.key||a.indexFormat!=b.indexFormat||a.indexOffset!=b.indexOffset||a.indexCount!=b.indexCount||
           a.instanceCount!=b.instanceCount||a.startIndex!=b.startIndex||a.startInstance!=b.startInstance||a.baseVertex!=b.baseVertex||
           a.blend.get()!=b.blend.get()||a.depth.get()!=b.depth.get()||a.raster.get()!=b.raster.get()||a.sampleMask!=b.sampleMask||
           a.stencilRef!=b.stencilRef||std::memcmp(a.blendFactor.data(),b.blendFactor.data(),16)||
           std::memcmp(&a.viewport,&b.viewport,sizeof(a.viewport))||std::memcmp(&a.scissor,&b.scissor,sizeof(a.scissor))||!same_resources(a,b))return false;
        for(unsigned stage=0;stage<2;++stage){const auto& shader=stage?a.pipeline->pixel:a.pipeline->vertex;
            for(unsigned slot=0;slot<14;++slot)if(shader.constantRegisters[slot]){
                const auto bytes=size_t(shader.constantRegisters[slot])*16;
                if(a.stages[stage].constants[slot].size()<bytes||b.stages[stage].constants[slot].size()<bytes||
                   std::memcmp(a.stages[stage].constants[slot].data(),b.stages[stage].constants[slot].data(),bytes))return false;
            }
        }
        return true;
    }
    void stop_watching(){watching_.store(false,std::memory_order_release);std::lock_guard<std::mutex> lock(resourceMutex_);sources_.clear();}
    bool watch_inputs(){
        std::lock_guard<std::mutex> lock(resourceMutex_);sources_.clear();
        const auto add=[&](ID3D11Resource* source){if(!source)return false;sources_.insert(source);return sources_.size()<=512;};
        for(const auto& eye:packets_)for(const auto& packet:eye){
            if(!add(packet.indices.source_resource()))return false;
            for(const auto& vertex:packet.vertices)if(!add(vertex.buffer.source_resource()))return false;
            for(const auto& stage:packet.stages)for(const auto& binding:stage.resources)if(binding.resource&&!add(binding.resource.source_resource()))return false;
        }
        changed_=false;watching_.store(true,std::memory_order_release);return true;
    }
    bool fallback(const char* reason){stop_watching();targets_.abort();batch_.abort();phase_=Phase::Fallback;reason_=reason;return false;}
public:
    StereoNativeReplacement()=default;
    StereoNativeReplacement(const StereoNativeReplacement&)=delete;
    StereoNativeReplacement& operator=(const StereoNativeReplacement&)=delete;
    // Caller forwards the original left draw when this returns false. A true
    // return means its exact output has already been composited at this point.
    bool left(ID3D11DeviceContext* context,const StereoFrameIdentity& key,
        ID3D11RenderTargetView* color,ID3D11DepthStencilView* depth,
        const StereoDrawPacket& actualLeft,const StereoDrawPacket& predictedRight,
        StereoQueryBoundary queries) noexcept {
        if(identity_.valid()&&identity_.thread!=GetCurrentThreadId())return false;
        if(!key.valid()||key.thread!=GetCurrentThreadId()||identity_==key||!context||queries!=StereoQueryBoundary::OutsideResultScopes)return false;
        try {
            stop_watching();targets_.abort();batch_.abort();identity_=key;context_=StereoComRef<ID3D11DeviceContext>(context);phase_=Phase::Fallback;
            if(!actualLeft.pipeline||actualLeft.key!=predictedRight.key||actualLeft.pipeline!=predictedRight.pipeline||
               !stereo_draw_detail::pairable(actualLeft,predictedRight))return fallback("native pair frozen inputs differ");
            D3D11_DEPTH_STENCIL_DESC state{};state.DepthEnable=TRUE;state.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;state.DepthFunc=D3D11_COMPARISON_LESS;
            if(actualLeft.depth)actualLeft.depth->GetDesc(&state);
            packets_={};packets_[0].push_back(actualLeft);packets_[1].push_back(predictedRight);
            if(!watch_inputs())return fallback("native pair resource watch unavailable");
            if(!targets_.prepare(context,key,color,depth,state.DepthFunc)||!batch_.prepare(context,key,targets_,packets_)||
               batch_.statistics().pairedDraws!=1||!batch_.execute(context,key,targets_))return fallback("native paired preparation/submission refused");
            if(changed_.load(std::memory_order_acquire)||!targets_.consume(0,key,color,depth,queries))return fallback("native left output refused");
            phase_=Phase::PendingRight;++counts_.pairedSubmissions;++counts_.leftDrawsRemoved;
            reason_="native left replaced; actual right inputs pending";return true;
        }catch(...){try{return fallback("native pair allocation failure");}catch(...){return false;}}
    }
    bool right(ID3D11DeviceContext* context,const StereoFrameIdentity& key,
        ID3D11RenderTargetView* color,ID3D11DepthStencilView* depth,
        const StereoDrawPacket& actualRight,StereoQueryBoundary queries) noexcept {
        if(phase_!=Phase::PendingRight||identity_!=key||context_.get()!=context||key.thread!=GetCurrentThreadId()||
           actualRight.key!=packets_[1][0].key)return false;
        try {
            if(changed_.load(std::memory_order_acquire)||!stereo_draw_detail::valid(actualRight,key,packets_[1][0].pipeline->device.get())||
               !same_draw(packets_[1][0],actualRight)){
                ++counts_.rightFallbacks;return fallback("actual right inputs/content version differ");
            }
            if(!targets_.consume(1,key,color,depth,queries)){++counts_.rightFallbacks;return fallback("native right output refused");}
            stop_watching();phase_=Phase::Finished;++counts_.rightDrawsRemoved;reason_="both native draws replaced by one paired submission";return true;
        }catch(...){try{++counts_.rightFallbacks;return fallback("native right comparison failure");}catch(...){return false;}}
    }
    void resource_written(ID3D11Resource* source) noexcept {
        if(!source||!watching_.load(std::memory_order_acquire))return;
        try{std::lock_guard<std::mutex> lock(resourceMutex_);if(sources_.count(source))changed_.store(true,std::memory_order_release);}
        catch(...){changed_.store(true,std::memory_order_release);}
    }
    void unknown_write() noexcept {if(watching_.load(std::memory_order_acquire))changed_.store(true,std::memory_order_release);}
    bool pending()const{return phase_==Phase::PendingRight;}
    void finish_right_group(){if(identity_.thread==GetCurrentThreadId()&&phase_==Phase::PendingRight){++counts_.rightFallbacks;fallback("native right draw absent; native visibility retained");}}
    void reset(){if(identity_.valid()&&identity_.thread!=GetCurrentThreadId())return;stop_watching();targets_.abort();batch_.abort();packets_={};context_.reset();identity_={};phase_=Phase::Empty;}
    const StereoNativeReplacementCounts& counts()const{return counts_;}
    const char* reason()const{return reason_;}
};
}
