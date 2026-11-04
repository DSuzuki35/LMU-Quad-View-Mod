#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <vector>

namespace lmuqv {
// Bounded scalar observations. No COM reference survives a snapshot; identities
// are borrowed from the audit's native-target namespace, not owned resources.
class StereoResourceTrace {
public:
    static constexpr size_t eventLimit=32768, useLimit=524288;
    static constexpr size_t usesPerEventLimit=5*D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT+8+1+64;
    enum class Operation {draw, dispatch, dispatchIndirect, copyResource, copyRegion, resolve};
    struct Use {
        uint32_t resource=0;
        const char* stage="copy";
        const char* binding="source";
        unsigned slot=0;
        const char* access="potential_read";
        unsigned dsvFlags=0;
    };
    struct Event {
        uint64_t sequence=0;
        unsigned frame=0;
        int slot=-1;
        Operation operation=Operation::draw;
        size_t firstUse=0,useCount=0;
        bool truncated=false,hasBox=false;
        unsigned sourceSubresource=0,destinationSubresource=0,format=0;
        std::array<UINT,3> destination{};
        D3D11_BOX box{};
    };
private:
    template<class T,size_t N=1> struct References {
        std::array<T*,N> values{};
        ~References(){for(auto* p:values)if(p)p->Release();}
        References()=default;
        References(const References&)=delete;
        References& operator=(const References&)=delete;
    };
    std::vector<Event> events_;
    std::vector<Use> uses_;
    uint64_t eventsDropped_=0,usesDropped_=0;
    bool aborted_=false;
    template<class Identity> void add(Event& event,ID3D11Resource* resource,
            const char* stage,const char* binding,unsigned slot,const char* access,
            Identity& identity,unsigned flags=0) {
        if(!resource)return;
        if(uses_.size()>=useLimit||event.useCount>=usesPerEventLimit){++usesDropped_;event.truncated=true;return;}
        uses_.push_back({identity(resource),stage,binding,slot,access,flags});++event.useCount;
    }
    template<class View,class Identity> void view(Event& event,View* value,
            const char* stage,const char* binding,unsigned slot,const char* access,
            Identity& identity,unsigned flags=0) {
        if(!value)return;
        References<ID3D11Resource> resource;
        value->GetResource(resource.values.data());
        add(event,resource.values[0],stage,binding,slot,access,identity,flags);
    }
    template<class Identity> void srvs(Event& event,ID3D11DeviceContext* context,
            unsigned stage,Identity& identity) {
        References<ID3D11ShaderResourceView,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> refs;
        const auto count=UINT(refs.values.size());auto** values=refs.values.data();
        switch(stage){
        case 0:context->VSGetShaderResources(0,count,values);break;
        case 1:context->HSGetShaderResources(0,count,values);break;
        case 2:context->DSGetShaderResources(0,count,values);break;
        case 3:context->GSGetShaderResources(0,count,values);break;
        case 4:context->PSGetShaderResources(0,count,values);break;
        default:context->CSGetShaderResources(0,count,values);break;
        }
        constexpr const char* names[]{"vs","hs","ds","gs","ps","cs"};
        for(unsigned i=0;i<count;++i)view(event,values[i],names[stage],"srv",i,"potential_read",identity);
    }
public:
    void reset() noexcept {events_.clear();uses_.clear();eventsDropped_=usesDropped_=0;aborted_=false;}
    void abort() noexcept {aborted_=true;}
    bool aborted() const noexcept {return aborted_;}
    bool available() noexcept {
        if(aborted_)return false;
        if(events_.size()>=eventLimit){++eventsDropped_;return false;}
        // Once exhausted, avoid querying resources for every subsequent draw.
        if(uses_.size()>=useLimit){++eventsDropped_;return false;}
        return true;
    }
    // Caller gates enabled/capture/thread/context and supplies the common
    // submission sequence. Every throw (including identity allocation) is
    // contained while RAII releases all Get* references.
    template<class Identity> void observe(Event event,ID3D11DeviceContext* context,
            D3D_FEATURE_LEVEL featureLevel,Identity identity,
            ID3D11Resource* source=nullptr,ID3D11Resource* destination=nullptr) noexcept {
        const auto initial=uses_.size();event.firstUse=initial;
        try {
            if(event.operation==Operation::copyResource||event.operation==Operation::copyRegion||event.operation==Operation::resolve){
                const char* stage=event.operation==Operation::resolve?"resolve":"copy";
                add(event,source,stage,"source",0,"potential_read",identity);
                add(event,destination,stage,"destination",0,"potential_write",identity);
            } else if(event.operation==Operation::draw) {
                for(unsigned stage=0;stage<5;++stage)srvs(event,context,stage,identity);
                References<ID3D11RenderTargetView,8> targets;
                References<ID3D11DepthStencilView> depth;
                context->OMGetRenderTargets(8,targets.values.data(),depth.values.data());
                // Blend/load/depth testing may read bound targets. Bindings alone
                // establish neither access nor shader execution.
                for(unsigned i=0;i<8;++i)view(event,targets.values[i],"om","rtv",i,"potential_read_write",identity);
                if(depth.values[0]){
                    D3D11_DEPTH_STENCIL_VIEW_DESC desc{};depth.values[0]->GetDesc(&desc);
                    const bool stencil=desc.Format==DXGI_FORMAT_D24_UNORM_S8_UINT||desc.Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                    const bool readOnly=(desc.Flags&D3D11_DSV_READ_ONLY_DEPTH)&&(!stencil||(desc.Flags&D3D11_DSV_READ_ONLY_STENCIL));
                    view(event,depth.values[0],"om","dsv",0,readOnly?"potential_read":"potential_read_write",identity,desc.Flags);
                }
                if(featureLevel>=D3D_FEATURE_LEVEL_11_0){
                    References<ID3D11UnorderedAccessView,64> uavs;
                    const unsigned count=featureLevel>=D3D_FEATURE_LEVEL_11_1?64:8;
                    context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,count,uavs.values.data());
                    for(unsigned i=0;i<count;++i)view(event,uavs.values[i],"om","uav",i,"potential_read_write",identity);
                }
            } else {
                srvs(event,context,5,identity);
                References<ID3D11UnorderedAccessView,64> uavs;
                const unsigned count=featureLevel>=D3D_FEATURE_LEVEL_11_1?64:featureLevel>=D3D_FEATURE_LEVEL_11_0?8:1;
                context->CSGetUnorderedAccessViews(0,count,uavs.values.data());
                for(unsigned i=0;i<count;++i)view(event,uavs.values[i],"cs","uav",i,"potential_read_write",identity);
            }
            events_.push_back(event);
        } catch(...) {uses_.resize(initial);aborted_=true;}
    }
    void write(std::ostream& out) const {
        out<<",\"resource_uses\":{\"coverage\":\"partial\",\"scheduling_eligible\":false,\"observation\":\"Submission-order potential accesses, not proof of execution or first actual consumer. Native target identity namespace; address reuse may alias lifetimes; no contents or ownership preserved.\",\"unobserved\":\"Clears, mip generation, updates/maps, Context1 and extended resolve API variants, indirect argument contents, IA/CB/SO resources, deferred/foreign contexts, runtime method implementation variants and non-owner threads; commands outside capture.\",\"srv_slots_per_stage\":128,\"uav_slots\":\"64 on FL11.1+, 8 on FL11.0 (CS 1 below); OM only FL11+\",\"event_limit\":"<<eventLimit
           <<",\"use_limit\":"<<useLimit<<",\"uses_per_event_limit\":"<<usesPerEventLimit
           <<",\"events_dropped\":"<<eventsDropped_<<",\"uses_dropped\":"<<usesDropped_
           <<",\"truncated\":"<<((eventsDropped_||usesDropped_)?"true":"false")
           <<",\"aborted\":"<<(aborted_?"true":"false")
           <<",\"abort_reason\":"<<(aborted_?"\"diagnostic exception or allocation failure\"":"null")<<",\"events\":[";
        constexpr const char* operations[]{"draw","dispatch","dispatch_indirect","copy_resource","copy_region","resolve_subresource"};
        bool first=true;
        for(const auto& event:events_){
            if(!first)out<<',';first=false;
            out<<"{\"sequence\":"<<event.sequence<<",\"frame\":"<<event.frame<<",\"slot\":"<<event.slot<<",\"operation\":\""<<operations[unsigned(event.operation)]<<"\",\"truncated\":"<<(event.truncated?"true":"false");
            if(event.operation==Operation::copyRegion){
                out<<",\"source_subresource\":"<<event.sourceSubresource<<",\"destination_subresource\":"<<event.destinationSubresource
                   <<",\"destination_xyz\":["<<event.destination[0]<<','<<event.destination[1]<<','<<event.destination[2]<<"],\"source_box\":";
                if(event.hasBox)out<<'['<<event.box.left<<','<<event.box.top<<','<<event.box.front<<','<<event.box.right<<','<<event.box.bottom<<','<<event.box.back<<']';else out<<"null";
            }
            if(event.operation==Operation::resolve)
                out<<",\"source_subresource\":"<<event.sourceSubresource<<",\"destination_subresource\":"<<event.destinationSubresource<<",\"format\":"<<event.format;
            out<<",\"uses\":[";
            for(size_t i=0;i<event.useCount;++i){if(i)out<<',';const auto& use=uses_[event.firstUse+i];
                out<<"{\"resource\":"<<use.resource<<",\"stage\":\""<<use.stage<<"\",\"binding\":\""<<use.binding<<"\",\"slot\":"<<use.slot<<",\"access\":\""<<use.access<<"\"";
                if(use.binding==std::string_view("dsv"))out<<",\"dsv_flags\":"<<use.dsvFlags;
                out<<'}';}
            out<<"]}";
        }
        out<<"]}";
    }
};
}
