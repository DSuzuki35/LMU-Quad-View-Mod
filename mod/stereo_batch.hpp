#pragma once
#include "stereo_draw.hpp"
#include "stereo_batch_plan.hpp"
#include "stereo_constant_arena.hpp"
#include "stereo_targets.hpp"
#include "stereo_command_state.hpp"

namespace lmuqv {
struct StereoBatchStatistics {size_t pairedDraws=0,monoDraws=0,constantMaps=0,constantBytes=0,stateCalls=0,contextCreations=0;};
// GPU backend for OWNED packets. No native game hooks or suppression decisions.
// A failed preparation touches only private resources. A ready command list is
// single-use; target copies are consumed explicitly at the original eye boundary.
class StereoBatch {
    enum class Phase {Empty,Preparing,Ready,Submitted,Aborted};
    Phase phase_=Phase::Empty;
    StereoFrameIdentity identity_;
    StereoComRef<ID3D11DeviceContext> immediate_;
    StereoComRef<ID3D11CommandList> commands_;
    StereoComRef<ID3D11Device> recordingDevice_;
    StereoComRef<ID3D11DeviceContext> recording_;
    StereoComRef<ID3D11DeviceContext1> recording1_;
    bool emulateOffsets_=false;
    size_t contextCreations_=0;
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
                const auto& source=left.stages[stage].constant(slot);
                allocations[stage][slot]=constants_.add(source.data(),size_t(shader.constantRegisters[slot])*16);if(!allocations[stage][slot])return false;
            }
            if(right){auto packed=pack_stereo_constants_from(shader,[&](unsigned eye,unsigned slot)->const std::vector<uint8_t>&{
                    return (eye?right->stages[stage]:left.stages[stage]).constant(slot);});if(!packed.complete)return false;
                if(!packed.bytes.empty()){allocations[stage][shader.packedSlot]=constants_.add(packed.bytes);if(!allocations[stage][shader.packedSlot])return false;}}
        }
        return true;
    }
    template<class Targets>void record(ID3D11DeviceContext* context,StereoCommandState& state,const StereoDrawPacket& d,
        bool paired,unsigned eye,const Allocations& allocations,Targets& targets){
        state.draw(d,paired,targets.color_target(paired?2:eye),targets.depth_target(paired?2:eye));
        for(unsigned stage=0;stage<2;++stage){
            std::array<ID3D11Buffer*,14> buffers{};std::array<UINT,14> first{},count{};
            for(unsigned slot=0;slot<14;++slot)if(allocations[stage][slot]){const auto b=constants_.resolve(allocations[stage][slot]);buffers[slot]=b.buffer;first[slot]=b.first;count[slot]=b.count;}
            state.constants(stage,buffers,first,count);
        }
        if(paired)submit_stereo_indexed(context,d.indexCount,d.instanceCount,d.startIndex,d.baseVertex,d.startInstance);
        else context->DrawIndexedInstanced(d.indexCount,d.instanceCount,d.startIndex,d.baseVertex,d.startInstance);
    }
public:
    StereoBatch()=default;
    StereoBatch(const StereoBatch&)=delete;
    StereoBatch& operator=(const StereoBatch&)=delete;
    template<class Targets,class Input>bool prepare(ID3D11DeviceContext* context,const StereoFrameIdentity& key,Targets& targets,
        const Input& input,bool forceOrdinaryConstants=false) noexcept {
        if(identity_.valid()&&identity_.thread!=GetCurrentThreadId()){reason_="foreign batch thread";return false;}
        if(phase_==Phase::Ready||phase_==Phase::Preparing){reason_="batch still in flight";return false;}
        const auto fail=[&](const char* reason){commands_.reset();target_.reset();constants_.abort();phase_=Phase::Aborted;reason_=reason;statistics_={};return false;};
        if(!context||!key.valid()||key.thread!=GetCurrentThreadId()||!targets.matches(context,key)||
           input[0].size()>stereoBatchDrawLimit||input[1].size()>stereoBatchDrawLimit)return fail("invalid batch/target scope");
        StereoComRef<ID3D11Device> device;context->GetDevice(device.put());StereoComRef<ID3D11Predicate> predicate;BOOL predicated=FALSE;
        context->GetPredication(predicate.put(),&predicated);if(predicate)return fail("predicated context cannot record SPS");
        phase_=Phase::Preparing;identity_=key;statistics_={};commands_.reset();target_.reset();
        try {
            for(const auto& eye:input)for(const auto& d:eye)if(!stereo_draw_detail::valid(d,key,device.get()))return fail("incomplete/unsupported owned draw");
            if(!targets.accepts_packets(input,key))return fail("target packet domain refused");
            std::array<std::vector<StereoBatchDraw>,2> order;
            std::unordered_map<uint64_t,size_t> rightIndex;
            for(size_t i=0;i<input[1].size();++i)if(!rightIndex.emplace(input[1][i].key,i).second)return fail("duplicate right draw key");
            for(unsigned eye=0;eye<2;++eye){order[eye].reserve(input[eye].size());for(const auto& d:input[eye])order[eye].push_back({d.key,0});}
            for(size_t i=0;i<input[0].size();++i){const auto found=rightIndex.find(input[0][i].key);
                if(found!=rightIndex.end()&&stereo_draw_detail::pairable(input[0][i],input[1][found->second])){
                    order[0][i].pairClass=1;order[1][found->second].pairClass=1;}}
            const auto plan=plan_stereo_batch(order[0],order[1]);if(!plan.complete)return fail(plan.reason);
            if(recordingDevice_.get()!=device.get()||!recording_){
                recording_.reset();recording1_.reset();recordingDevice_.reset();
                if(FAILED(device->CreateDeferredContext(0,recording_.put())))return fail("deferred SPS context unavailable");
                recording_->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(recording1_.put()));
                D3D11_FEATURE_DATA_THREADING threading{};
                emulateOffsets_=FAILED(device->CheckFeatureSupport(D3D11_FEATURE_THREADING,&threading,sizeof(threading)))||!threading.DriverCommandLists;
                recordingDevice_=device;++contextCreations_;
            }
            const bool hasOffsets=bool(recording1_);
            if(!constants_.begin(context,key,8*1024*1024,forceOrdinaryConstants||!hasOffsets))return fail(constants_.reason());
            std::vector<PreparedAction> prepared;prepared.reserve(plan.actions.size());
            for(const auto action:plan.actions){PreparedAction p;p.draw=action;const bool paired=action.kind==StereoBatchActionKind::Pair;
                const auto& d=action.kind==StereoBatchActionKind::Right?input[1][action.right]:input[0][action.left];
                const auto* right=paired?&input[1][action.right]:nullptr;
                if(!prepare_constants(d,right,p.constants))return fail("constant preparation refused");prepared.push_back(std::move(p));}
            if(!constants_.seal())return fail(constants_.reason());generation_=constants_.generation();
            recording_->ClearState();StereoCommandState state(recording_.get(),recording1_.get(),constants_.uses_offsets(),emulateOffsets_);
            for(const auto& p:prepared){const bool paired=p.draw.kind==StereoBatchActionKind::Pair;const bool right=p.draw.kind==StereoBatchActionKind::Right;
                const auto& d=right?input[1][p.draw.right]:input[0][p.draw.left];record(recording_.get(),state,d,paired,right?1:0,p.constants,targets);}
            if(FAILED(recording_->FinishCommandList(FALSE,commands_.put()))||FAILED(device->GetDeviceRemovedReason()))return fail("stereo command recording failed");
            immediate_=StereoComRef<ID3D11DeviceContext>(context);target_=StereoComRef<ID3D11RenderTargetView>(targets.color_target(2));
            statistics_={plan.paired,plan.actions.size()-plan.paired,constants_.map_count(),constants_.packed_bytes(),state.calls(),contextCreations_};
            phase_=Phase::Ready;reason_="owned stereo commands prepared";return true;
        }catch(...){return fail("batch preparation allocation failure");}
    }
    template<class Targets>bool execute(ID3D11DeviceContext* context,const StereoFrameIdentity& key,Targets& targets){
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
