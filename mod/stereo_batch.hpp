#pragma once
#include "stereo_draw.hpp"
#include "stereo_batch_plan.hpp"
#include "stereo_constant_arena.hpp"
#include "stereo_targets.hpp"

namespace lmuqv {
struct StereoBatchStatistics {size_t pairedDraws=0,monoDraws=0,constantMaps=0,constantBytes=0;};
// GPU backend for OWNED packets. No native game hooks or suppression decisions.
// A failed preparation touches only private resources. A ready command list is
// single-use; target copies are consumed explicitly at the original eye boundary.
class StereoBatch {
    enum class Phase {Empty,Preparing,Ready,Submitted,Aborted};
    Phase phase_=Phase::Empty;
    StereoFrameIdentity identity_;
    StereoComRef<ID3D11DeviceContext> immediate_;
    StereoComRef<ID3D11CommandList> commands_;
    StereoComRef<ID3D11RenderTargetView> target_;
    StereoConstantArena constants_;
    uint64_t generation_=0;
    StereoBatchStatistics statistics_;
    const char* reason_="no stereo batch";
    using Allocations=std::array<std::array<StereoConstantAllocation,14>,2>;
    struct PreparedAction {StereoBatchAction draw;Allocations constants;};

    bool prepare_constants(const StereoDrawPacket& left,const StereoDrawPacket* right,Allocations& allocations){
        for(unsigned stage=0;stage<2;++stage){const auto& shader=stage?left.pipeline->pixel:left.pipeline->vertex;
            for(unsigned slot=0;slot<14;++slot)if(shader.constantRegisters[slot]){
                const auto& source=left.stages[stage].constants[slot];
                std::vector<uint8_t> used(source.begin(),source.begin()+size_t(shader.constantRegisters[slot])*16);
                allocations[stage][slot]=constants_.add(used);if(!allocations[stage][slot])return false;
            }
            if(right){const std::array<StereoConstantBuffers,2> source{left.stages[stage].constants,right->stages[stage].constants};
                auto packed=pack_stereo_constants(shader,source);if(!packed.complete)return false;
                allocations[stage][shader.packedSlot]=constants_.add(packed.bytes);if(!allocations[stage][shader.packedSlot])return false;}
        }
        return true;
    }
    void record(ID3D11DeviceContext* context,ID3D11DeviceContext1* context1,const StereoDrawPacket& d,
        bool paired,unsigned eye,const Allocations& allocations,StereoTargetBridge& targets,bool emulateOffsets){
        auto* rt=targets.color_target(paired?2:eye);context->OMSetRenderTargets(1,&rt,targets.depth_target(paired?2:eye));
        context->OMSetBlendState(d.blend.get(),d.blendFactor.data(),d.sampleMask);context->OMSetDepthStencilState(d.depth.get(),d.stencilRef);
        context->RSSetState(d.raster.get());context->RSSetViewports(1,&d.viewport);context->RSSetScissorRects(1,&d.scissor);
        std::array<ID3D11Buffer*,32> vertices{};std::array<UINT,32> strides{},offsets{};
        for(const auto& v:d.vertices){vertices[v.slot]=static_cast<ID3D11Buffer*>(v.buffer.resource());strides[v.slot]=v.stride;offsets[v.slot]=v.offset;}
        context->IASetVertexBuffers(0,32,vertices.data(),strides.data(),offsets.data());
        context->IASetIndexBuffer(static_cast<ID3D11Buffer*>(d.indices.resource()),d.indexFormat,d.indexOffset);
        context->IASetInputLayout(paired?d.pipeline->pairedLayout.get():d.pipeline->monoLayout.get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(paired?d.pipeline->pairedVs.get():d.pipeline->monoVs.get(),nullptr,0);
        context->PSSetShader(paired?d.pipeline->pairedPs.get():d.pipeline->monoPs.get(),nullptr,0);
        context->GSSetShader(paired?d.pipeline->geometry.get():nullptr,nullptr,0);
        for(unsigned stage=0;stage<2;++stage){
            std::array<ID3D11Buffer*,14> buffers{};std::array<UINT,14> first{},count{};
            for(unsigned slot=0;slot<14;++slot)if(allocations[stage][slot]){const auto b=constants_.resolve(allocations[stage][slot]);buffers[slot]=b.buffer;first[slot]=b.first;count[slot]=b.count;}
            std::array<ID3D11ShaderResourceView*,128> resources{};std::array<ID3D11SamplerState*,16> samplers{};
            for(const auto& r:d.stages[stage].resources)resources[r.slot]=r.resource.view();
            for(const auto& s:d.stages[stage].samplers)samplers[s.slot]=s.state.get();
            // D3D11 command-list emulation may discard offset-only changes.
            // Null the old bindings, then bind a range whose two edges are both
            // non-null. Merely nulling slots 0/13 fails for sparse PS bindings.
            // See Microsoft's VSSetConstantBuffers1 command-list workaround.
            UINT begin=0,end=14;
            if(constants_.uses_offsets()&&emulateOffsets){const std::array<ID3D11Buffer*,14> empty{};
                if(stage)context->PSSetConstantBuffers(0,14,empty.data());else context->VSSetConstantBuffers(0,14,empty.data());
                while(begin<end&&!buffers[begin])++begin;while(end>begin&&!buffers[end-1])--end;}
            if(stage){if(constants_.uses_offsets()){if(end>begin)context1->PSSetConstantBuffers1(begin,end-begin,buffers.data()+begin,first.data()+begin,count.data()+begin);}else context->PSSetConstantBuffers(0,14,buffers.data());
                context->PSSetShaderResources(0,128,resources.data());context->PSSetSamplers(0,16,samplers.data());}
            else {if(constants_.uses_offsets()){if(end>begin)context1->VSSetConstantBuffers1(begin,end-begin,buffers.data()+begin,first.data()+begin,count.data()+begin);}else context->VSSetConstantBuffers(0,14,buffers.data());
                context->VSSetShaderResources(0,128,resources.data());context->VSSetSamplers(0,16,samplers.data());}
        }
        if(paired)submit_stereo_indexed(context,d.indexCount,d.instanceCount,d.startIndex,d.baseVertex,d.startInstance);
        else context->DrawIndexedInstanced(d.indexCount,d.instanceCount,d.startIndex,d.baseVertex,d.startInstance);
    }
public:
    StereoBatch()=default;
    StereoBatch(const StereoBatch&)=delete;
    StereoBatch& operator=(const StereoBatch&)=delete;
    bool prepare(ID3D11DeviceContext* context,const StereoFrameIdentity& key,StereoTargetBridge& targets,
        const std::array<std::vector<StereoDrawPacket>,2>& input,bool forceOrdinaryConstants=false) noexcept {
        if(identity_.valid()&&identity_.thread!=GetCurrentThreadId()){reason_="foreign batch thread";return false;}
        if(phase_==Phase::Ready||phase_==Phase::Preparing){reason_="batch still in flight";return false;}
        const auto fail=[&](const char* reason){commands_.reset();target_.reset();constants_.abort();phase_=Phase::Aborted;reason_=reason;statistics_={};return false;};
        if(!context||!key.valid()||key.thread!=GetCurrentThreadId()||!targets.matches(context,key)||
           input[0].size()>stereoBatchDrawLimit||input[1].size()>stereoBatchDrawLimit)return fail("invalid batch/target scope");
        StereoComRef<ID3D11Device> device;context->GetDevice(device.put());StereoComRef<ID3D11Predicate> predicate;BOOL predicated=FALSE;
        context->GetPredication(predicate.put(),&predicated);if(predicate)return fail("predicated native scope requires fallback");
        phase_=Phase::Preparing;identity_=key;statistics_={};commands_.reset();target_.reset();
        try {
            for(const auto& eye:input)for(const auto& d:eye)if(!stereo_draw_detail::valid(d,key,device.get()))return fail("incomplete/unsupported owned draw");
            std::array<std::vector<StereoBatchDraw>,2> order;
            std::unordered_map<uint64_t,size_t> rightIndex;
            for(size_t i=0;i<input[1].size();++i)if(!rightIndex.emplace(input[1][i].key,i).second)return fail("duplicate right draw key");
            for(unsigned eye=0;eye<2;++eye){order[eye].reserve(input[eye].size());for(const auto& d:input[eye])order[eye].push_back({d.key,0});}
            for(size_t i=0;i<input[0].size();++i){const auto found=rightIndex.find(input[0][i].key);
                if(found!=rightIndex.end()&&stereo_draw_detail::pairable(input[0][i],input[1][found->second])){
                    order[0][i].pairClass=1;order[1][found->second].pairClass=1;}}
            const auto plan=plan_stereo_batch(order[0],order[1]);if(!plan.complete)return fail(plan.reason);
            StereoComRef<ID3D11DeviceContext> deferred;StereoComRef<ID3D11DeviceContext1> deferred1;
            if(FAILED(device->CreateDeferredContext(0,deferred.put())))return fail("deferred batch context unavailable");
            const bool hasOffsets=SUCCEEDED(deferred->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(deferred1.put())));
            D3D11_FEATURE_DATA_THREADING threading{};
            const bool emulateOffsets=FAILED(device->CheckFeatureSupport(D3D11_FEATURE_THREADING,&threading,sizeof(threading)))||!threading.DriverCommandLists;
            if(!constants_.begin(context,key,8*1024*1024,forceOrdinaryConstants||!hasOffsets))return fail(constants_.reason());
            std::vector<PreparedAction> prepared;prepared.reserve(plan.actions.size());
            for(const auto action:plan.actions){PreparedAction p;p.draw=action;const bool paired=action.kind==StereoBatchActionKind::Pair;
                const auto& d=action.kind==StereoBatchActionKind::Right?input[1][action.right]:input[0][action.left];
                const auto* right=paired?&input[1][action.right]:nullptr;
                if(!prepare_constants(d,right,p.constants))return fail("constant preparation refused");prepared.push_back(std::move(p));}
            if(!constants_.seal())return fail(constants_.reason());generation_=constants_.generation();
            deferred->ClearState();
            for(const auto& p:prepared){const bool paired=p.draw.kind==StereoBatchActionKind::Pair;const bool right=p.draw.kind==StereoBatchActionKind::Right;
                const auto& d=right?input[1][p.draw.right]:input[0][p.draw.left];record(deferred.get(),deferred1.get(),d,paired,right?1:0,p.constants,targets,emulateOffsets);}
            if(FAILED(deferred->FinishCommandList(FALSE,commands_.put()))||FAILED(device->GetDeviceRemovedReason()))return fail("stereo command recording failed");
            immediate_=StereoComRef<ID3D11DeviceContext>(context);target_=StereoComRef<ID3D11RenderTargetView>(targets.color_target(2));
            statistics_={plan.paired,plan.actions.size()-plan.paired,constants_.map_count(),constants_.packed_bytes()};
            phase_=Phase::Ready;reason_="owned stereo commands prepared";return true;
        }catch(...){return fail("batch preparation allocation failure");}
    }
    bool execute(ID3D11DeviceContext* context,const StereoFrameIdentity& key,StereoTargetBridge& targets){
        if(phase_!=Phase::Ready||context!=immediate_.get()||key!=identity_||key.thread!=GetCurrentThreadId()||
           !targets.matches(context,key)||targets.color_target(2)!=target_.get()||!constants_.ready(key,generation_))return false;
        StereoComRef<ID3D11Predicate> predicate;BOOL value=FALSE;context->GetPredication(predicate.put(),&value);if(predicate){reason_="predication changed before batch execution";return false;}
        // Runtime restores the immediate context's application pipeline state.
        context->ExecuteCommandList(commands_.get(),TRUE);constants_.mark_submitted(key);targets.mark_rendered(key);
        phase_=Phase::Submitted;commands_.reset();reason_="owned stereo batch submitted";return true;
    }
    bool ready()const{return phase_==Phase::Ready;}
    const StereoBatchStatistics& statistics()const{return statistics_;}
    const char* reason()const{return reason_;}
    void abort(){if(!identity_.valid()||identity_.thread==GetCurrentThreadId()){commands_.reset();target_.reset();constants_.abort();phase_=Phase::Aborted;}}
};
}
