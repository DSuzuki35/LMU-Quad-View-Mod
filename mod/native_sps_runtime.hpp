#pragma once
#include "native_stereo_draw_inputs.hpp"
#include "native_stereo_view_inputs.hpp"
#include "stereo_native_registry.hpp"
#include "stereo_native_replacement.hpp"
#include <atomic>
#include <unordered_map>
#include <cstdio>

namespace lmuqv {
namespace native_sps_detail {
inline thread_local unsigned internalDepth=0;
struct Internal {Internal(){++internalDepth;}~Internal(){--internalDepth;}Internal(const Internal&)=delete;};
struct Group {
    bool active=false,preparing=false,prepared=false,restore=false;
    unsigned eye=2,helpers=0;
    uintptr_t renderer=0,view=0,mesh=0,shader=0;
    StereoFrameIdentity identity;
    NativeStereoViewInputs views;
    NativeStereoTransformSnapshot transform;
    bool(*readable)(const void*,size_t)=nullptr;
};
}
class NativeSpsRuntime {
    StereoComRef<ID3D11DeviceContext> context_;
    StereoComRef<ID3D11Device> device_;
    std::array<StereoComRef<IUnknown>,8> contextAliases_,deviceAliases_;
    unsigned contextAliasCount_=0,deviceAliasCount_=0;
    uintptr_t image_=0,selectedMesh_=0;
    std::atomic<bool> enabled_{false},queryKnown_{false},foreign_{false};
    std::atomic<uint32_t> thread_{0};
    std::mutex queryMutex_;
    std::unordered_map<ID3D11Asynchronous*,uint32_t> queries_;
    native_sps_detail::Group group_;
    StereoFrameIdentity frame_,warmed_;
    unsigned attempts_=0;
    bool frameAttempted_=false;
    bool pairedLogged_=false;
    std::unique_ptr<StereoGpuResourcePool> pool_;
    StereoNativeRegistry registry_;
    std::unique_ptr<StereoNativeReplacement> replacement_;
    std::recursive_mutex apiMutex_;
    std::vector<StereoComRef<ID3D11Resource>> graphicsWrites_,computeWrites_;
    bool graphicsValid_=false,computeValid_=false;
    uint64_t captures_=0,refusals_=0;
    std::array<uint64_t,2> groupsSeen_{},preparationsSeen_{},preparedSeen_{};
    uint64_t drawCandidates_=0,queryRefusals_=0,pipelineUnavailable_=0;
    uint64_t commandLists_=0;
    bool progressLogged_=false;
    void(*logger_)(const char*)=nullptr;
    static uint32_t constant_mask(const StereoShaderVariant& shader){uint32_t mask=0;for(unsigned i=0;i<14;++i)if(shader.constantRegisters[i])mask|=1u<<i;return mask;}
    bool queries_safe() noexcept {
        if(!queryKnown_.load()||foreign_.load())return false;
        try{std::lock_guard<std::mutex> lock(queryMutex_);for(const auto& q:queries_)if(q.second!=D3D11_QUERY_TIMESTAMP_DISJOINT)return false;return true;}
        catch(...){return false;}
    }
    static bool equal_constants(const StereoPipeline& p,const std::array<StereoConstantBuffers,2>& a,
        const std::array<StereoConstantBuffers,2>& b){
        for(unsigned stage=0;stage<2;++stage){const auto& shader=stage?p.pixel:p.vertex;
            for(unsigned slot=0;slot<14;++slot)if(shader.constantRegisters[slot]){const auto bytes=size_t(shader.constantRegisters[slot])*16;
                if(a[stage][slot].size()<bytes||b[stage][slot].size()<bytes||std::memcmp(a[stage][slot].data(),b[stage][slot].data(),bytes))return false;}}
        return true;
    }
public:
    bool configure_early(ID3D11DeviceContext* context,uintptr_t image,void(*logger)(const char*)){
        std::lock_guard<std::recursive_mutex> lock(apiMutex_);
        if(!context||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||enabled_)return false;
        context_=StereoComRef<ID3D11DeviceContext>(context);context_->GetDevice(device_.put());if(!device_)return false;
        contextAliases_={};deviceAliases_={};contextAliasCount_=deviceAliasCount_=0;
        contextAliases_[contextAliasCount_++]=StereoComRef<IUnknown>(context);deviceAliases_[deviceAliasCount_++]=StereoComRef<IUnknown>(device_.get());
        pool_=std::make_unique<StereoGpuResourcePool>();replacement_=std::make_unique<StereoNativeReplacement>();
        captures_=refusals_=0;pairedLogged_=false;groupsSeen_=preparationsSeen_=preparedSeen_={};
        drawCandidates_=queryRefusals_=pipelineUnavailable_=commandLists_=0;progressLogged_=false;
        image_=image;logger_=logger;queryKnown_=true;foreign_=false;enabled_=true;return true;
    }
    std::unique_lock<std::recursive_mutex> api_lock(ID3D11DeviceContext* context){
        if(native_sps_detail::internalDepth||!this->context(context))return {};
        return std::unique_lock<std::recursive_mutex>(apiMutex_);
    }
    bool enabled()const{return enabled_.load(std::memory_order_relaxed);}
    bool observing_group()const noexcept{return enabled()&&thread_.load()==GetCurrentThreadId()&&group_.active;}
    bool context(ID3D11DeviceContext* context)const{if(!enabled())return false;for(unsigned i=0;i<contextAliasCount_;++i)if(contextAliases_[i].get()==static_cast<IUnknown*>(context))return true;return false;}
    bool device(ID3D11Device* device)const{if(!enabled())return false;for(unsigned i=0;i<deviceAliasCount_;++i)if(deviceAliases_[i].get()==static_cast<IUnknown*>(device))return true;return false;}
    void add_context_alias(ID3D11DeviceContext* context){for(unsigned i=0;i<contextAliasCount_;++i)if(contextAliases_[i].get()==static_cast<IUnknown*>(context))return;
        if(contextAliasCount_<contextAliases_.size())contextAliases_[contextAliasCount_++]=StereoComRef<IUnknown>(context);}
    void add_device_alias(ID3D11Device* device){for(unsigned i=0;i<deviceAliasCount_;++i)if(deviceAliases_[i].get()==static_cast<IUnknown*>(device))return;
        if(deviceAliasCount_<deviceAliases_.size())deviceAliases_[deviceAliasCount_++]=StereoComRef<IUnknown>(device);}
    ID3D11DeviceContext* selected_context()const{return context_.get();}
    void observe_shader(ID3D11Device* device,ID3D11DeviceChild* shader,const void* code,size_t bytes,bool vertex){
        if(native_sps_detail::internalDepth||!enabled())return;
        std::lock_guard<std::recursive_mutex> lock(apiMutex_);
        if(!native_sps_detail::internalDepth&&this->device(device))registry_.shader(shader,code,bytes,vertex);
    }
    void observe_layout(ID3D11Device* device,ID3D11InputLayout* layout,const D3D11_INPUT_ELEMENT_DESC* elements,UINT count){
        if(native_sps_detail::internalDepth||!enabled())return;
        std::lock_guard<std::recursive_mutex> lock(apiMutex_);
        if(!native_sps_detail::internalDepth&&this->device(device))registry_.layout(layout,elements,count);
    }
    void begin_query(ID3D11DeviceContext* context,ID3D11Asynchronous* query) noexcept {
        if(native_sps_detail::internalDepth||!this->context(context))return;
        try{std::lock_guard<std::mutex> lock(queryMutex_);StereoComRef<ID3D11Query> actual;D3D11_QUERY_DESC desc{};
            if(!query||FAILED(query->QueryInterface(__uuidof(ID3D11Query),reinterpret_cast<void**>(actual.put())))){queryKnown_=false;return;}
            actual->GetDesc(&desc);if(!queries_.emplace(query,UINT(desc.Query)).second)queryKnown_=false;
        }catch(...){queryKnown_=false;}
    }
    void end_query(ID3D11DeviceContext* context,ID3D11Asynchronous* query) noexcept {
        if(native_sps_detail::internalDepth||!this->context(context))return;
        try{std::lock_guard<std::mutex> lock(queryMutex_);queries_.erase(query);}catch(...){queryKnown_=false;}
    }
    void foreign_context_use(ID3D11DeviceContext* context) noexcept {
        if(native_sps_detail::internalDepth||!enabled())return;
        if(this->context(context)&&thread_.load()&&thread_.load()!=GetCurrentThreadId()){
            foreign_=true;replacement_->unknown_write();
        }
    }
    void written(ID3D11DeviceContext* context,ID3D11Resource* resource) noexcept {
        if(native_sps_detail::internalDepth||!this->context(context))return;
        foreign_context_use(context);replacement_->resource_written(resource);
    }
    void unknown_commands(ID3D11DeviceContext* context) noexcept {
        if(native_sps_detail::internalDepth||!this->context(context))return;
        foreign_context_use(context);replacement_->unknown_write();queryKnown_=false;
    }
    void command_list(ID3D11DeviceContext* context) noexcept {
        if(native_sps_detail::internalDepth||!this->context(context))return;
        foreign_context_use(context);replacement_->unknown_write();++commandLists_;
        // FinishCommandList terminates deferred query ranges. Executing a
        // finished list cannot leave a new query range open on this context.
        // Keep all separately tracked immediate ranges; invalidate resources.
        // https://learn.microsoft.com/en-us/windows-hardware/drivers/display/supporting-command-lists
    }
    void gpu_writes(ID3D11DeviceContext* context,bool compute) noexcept {
        if(native_sps_detail::internalDepth||!this->context(context))return;
        foreign_context_use(context);if(thread_.load()!=GetCurrentThreadId()||!replacement_->pending())return;
        try {
            native_sps_detail::Internal internal;
            auto& writes=compute?computeWrites_:graphicsWrites_;auto& valid=compute?computeValid_:graphicsValid_;
            if(valid){for(const auto& resource:writes)replacement_->resource_written(resource.get());return;}
            writes.clear();
            const auto add=[&](ID3D11View* view){if(view){StereoComRef<ID3D11Resource> resource;view->GetResource(resource.put());
                replacement_->resource_written(resource.get());writes.push_back(std::move(resource));}};
            const UINT count=device_->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64:8;
            std::array<ID3D11UnorderedAccessView*,64> uav{};
            if(compute)context->CSGetUnorderedAccessViews(0,count,uav.data());
            else{
                std::array<ID3D11RenderTargetView*,8> targets{};StereoComRef<ID3D11DepthStencilView> depth;
                context->OMGetRenderTargets(UINT(targets.size()),targets.data(),depth.put());
                context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,count,uav.data());
                for(auto* view:targets)if(view){add(view);view->Release();}add(depth.get());
                std::array<ID3D11Buffer*,4> stream{};context->SOGetTargets(UINT(stream.size()),stream.data());
                for(auto* buffer:stream)if(buffer){replacement_->resource_written(buffer);writes.emplace_back(buffer);buffer->Release();}
            }
            for(auto* view:uav)if(view){add(view);view->Release();}valid=true;
        }catch(...){replacement_->unknown_write();}
    }
    void outputs_changed(ID3D11DeviceContext* context) noexcept {
        if(native_sps_detail::internalDepth||!this->context(context))return;
        foreign_context_use(context);graphicsValid_=computeValid_=false;
    }
    native_sps_detail::Group begin_group(ID3D11DeviceContext* context,uintptr_t renderer,uintptr_t view,unsigned eye,
        const NativeStereoViewInputs* views,bool(*readable)(const void*,size_t)){
        if(!enabled())return {};
        std::lock_guard<std::recursive_mutex> lock(apiMutex_);
        if(!enabled()||(thread_.load()&&thread_.load()!=GetCurrentThreadId()))return {};
        auto previous=std::move(group_);previous.restore=true;group_={};
        if(!this->context(context)||!views||!views->complete||!views->identity.valid()||views->identity.thread!=GetCurrentThreadId()||eye>1||!renderer||!view||!readable)return previous;
        if(thread_.load()&&thread_.load()!=GetCurrentThreadId()){foreign_=true;return previous;}thread_=GetCurrentThreadId();
        if(frame_!=views->identity){
            replacement_->finish_right_group();frame_=views->identity;attempts_=0;frameAttempted_=false;selectedMesh_=0;
            // Intercepted context mutations share apiMutex_. A completed worker
            // update invalidates its current pair, but a new frame can capture
            // fresh contents after that update has finished.
            foreign_=false;
        }
        ++groupsSeen_[eye];group_.active=true;group_.renderer=renderer;group_.view=view;group_.eye=eye;group_.views=*views;group_.identity=views->identity;group_.readable=readable;return previous;
    }
    void end_group(native_sps_detail::Group previous) noexcept {
        if(!previous.restore)return;
        try{
            std::lock_guard<std::recursive_mutex> lock(apiMutex_);
            if(!previous.restore)return;if(!enabled()){group_={};return;}
            if(group_.active){const auto key=group_.identity;const auto eye=group_.eye;group_.active=false;
                if(eye==1)replacement_->finish_right_group();
                if(warmed_!=key){native_sps_detail::Internal internal;registry_.warm_one(device_.get());warmed_=key;}}
            previous.restore=false;group_=std::move(previous);
        }catch(...){group_={};replacement_->unknown_write();}
    }
    void preparation_begin(uintptr_t shader,uintptr_t view,uintptr_t mesh,uint32_t,bool reset,uint32_t caller) noexcept {
        if(!enabled()||thread_.load()!=GetCurrentThreadId())return;
        group_.prepared=group_.preparing=false;group_.helpers=0;group_.transform={};
        if(!enabled()||!group_.active||view!=group_.view||!mesh||reset||
           (caller!=0x51f67e&&caller!=0x51f5c4))return;
        if(group_.eye==0){if(frameAttempted_||attempts_>=32)return;++attempts_;}
        else if(!replacement_->pending()||mesh!=selectedMesh_)return;
        ++preparationsSeen_[group_.eye];group_.preparing=true;group_.mesh=mesh;group_.shader=shader;
    }
    void transform(const NativeStereoTransformArgs& args) noexcept {
        if(!enabled()||thread_.load()!=GetCurrentThreadId()||!group_.active||!group_.preparing||group_.eye!=0||
           reinterpret_cast<uintptr_t>(args.mesh)!=group_.mesh||reinterpret_cast<uintptr_t>(args.view)!=group_.view)return;
        ++group_.helpers;if(group_.helpers!=1){group_.transform={};return;}
        NativeStereoFpGuard preserve;group_.transform=capture_native_stereo_transform(args,group_.readable);
    }
    void preparation_end(int result) noexcept {
        if(!enabled()||thread_.load()!=GetCurrentThreadId()||!group_.active||!group_.preparing)return;
        group_.preparing=false;group_.prepared=result!=0&&(group_.eye==1||(group_.helpers==1&&group_.transform.complete));
        if(group_.prepared)++preparedSeen_[group_.eye];
    }
    bool indexed(ID3D11DeviceContext* context,UINT indices,UINT instances,UINT start,INT base,UINT first) noexcept {
        if(native_sps_detail::internalDepth||!this->context(context)||thread_.load()!=GetCurrentThreadId()||!group_.active||!group_.prepared||
           group_.identity.thread!=GetCurrentThreadId()||!indices||!instances)return false;
        group_.prepared=false;++drawCandidates_;const bool querySafe=queries_safe();if(!querySafe)++queryRefusals_;
        try {
            NativeStereoFpGuard preserve;native_sps_detail::Internal internal;
            StereoComRef<ID3D11VertexShader> vs;StereoComRef<ID3D11PixelShader> ps;StereoComRef<ID3D11InputLayout> layout;
            context->VSGetShader(vs.put(),nullptr,nullptr);context->PSGetShader(ps.put(),nullptr,nullptr);context->IAGetInputLayout(layout.put());
            const auto pipeline=registry_.request({vs.get(),ps.get(),layout.get()});if(!pipeline||!pipeline->fullColorOutput){++pipelineUnavailable_;return false;}
            // Metadata requests and cache warmup remain observable even when
            // queries prevent GPU capture, so one run exposes both blockers.
            if(!querySafe)return false;
            StereoDrawCaptureRequest request;request.identity=group_.identity;request.key=group_.mesh;request.indexCount=indices;request.instanceCount=instances;
            request.startIndex=start;request.baseVertex=base;request.startInstance=first;request.pipeline=pipeline;request.vertex=vs.get();request.pixel=ps.get();request.layout=layout.get();request.queries=StereoQueryBoundary::OutsideResultScopes;
            if(group_.eye==0){if(frameAttempted_)return false;frameAttempted_=true;
                if(!pool_->begin(context,group_.identity))return false;
                const std::array<uint32_t,2> required{constant_mask(pipeline->vertex),constant_mask(pipeline->pixel)};
                const auto native=capture_native_stereo_constants(group_.renderer,image_,group_.identity,required,group_.readable);if(!native.complete){++refusals_;return false;}
                auto right=group_.views.eyes[1].transform;right.world=group_.transform.input.world;right.localTransform=group_.transform.input.localTransform;
                const auto constants=prepare_native_stereo_draw_constants({group_.transform.input,right},{native,native});
                if(!constants.complete||!equal_constants(*pipeline,native.stages,constants.eyes[0])){++refusals_;return false;}
                request.constants=&native.stages;request.constantTokens=&native.bufferTokens;
                auto actual=capture_stereo_draw(context,*pool_,request);if(!actual.complete){++refusals_;return false;}
                auto predicted=actual.packet;for(unsigned stage=0;stage<2;++stage)predicted.stages[stage].constants=constants.eyes[1][stage];
                StereoComRef<ID3D11RenderTargetView> color;StereoComRef<ID3D11DepthStencilView> depth;context->OMGetRenderTargets(1,color.put(),depth.put());
                ++captures_;if(!queries_safe())return false;
                if(replacement_->left(context,group_.identity,color.get(),depth.get(),actual.packet,predicted,request.queries)){selectedMesh_=group_.mesh;graphicsValid_=computeValid_=false;return true;}
                ++refusals_;return false;
            }
            if(!replacement_->pending()||group_.mesh!=selectedMesh_)return false;
            auto actual=capture_native_stereo_draw_inputs(context,*pool_,group_.renderer,image_,request,group_.readable);
            if(!actual.complete){replacement_->finish_right_group();++refusals_;return false;}
            StereoComRef<ID3D11RenderTargetView> color;StereoComRef<ID3D11DepthStencilView> depth;context->OMGetRenderTargets(1,color.put(),depth.put());
            const bool replaced=replacement_->right(context,group_.identity,color.get(),depth.get(),actual.packet,request.queries);
            if(replaced&&!pairedLogged_){pairedLogged_=true;if(logger_)logger_("sps_render active: both original native focus draws replaced by one paired submission; actual right inputs matched");}
            return replaced;
        }catch(...){++refusals_;replacement_->unknown_write();return false;}
    }
    void report_progress_once(){
        if(progressLogged_||!enabled())return;
        std::lock_guard<std::recursive_mutex> lock(apiMutex_);if(progressLogged_||!enabled())return;
        progressLogged_=true;NativeStereoFpGuard preserve;report_stages();
    }
    void report_stages(){
        if(!logger_)return;char line[768]{};const auto c=replacement_?replacement_->counts():StereoNativeReplacementCounts{};
        unsigned active=0,result=0;{std::lock_guard<std::mutex> lock(queryMutex_);active=unsigned(queries_.size());for(const auto& q:queries_)result+=q.second!=D3D11_QUERY_TIMESTAMP_DISJOINT;}
        std::snprintf(line,sizeof(line),"sps_render stages groups=%llu/%llu preparations=%llu/%llu prepared=%llu/%llu candidates=%llu query_refused=%llu pipeline_unavailable=%llu pipelines=%llu paired=%llu left_removed=%llu right_removed=%llu query_known=%d foreign=%d active_queries=%u result_queries=%u command_lists=%llu",
            static_cast<unsigned long long>(groupsSeen_[0]),static_cast<unsigned long long>(groupsSeen_[1]),
            static_cast<unsigned long long>(preparationsSeen_[0]),static_cast<unsigned long long>(preparationsSeen_[1]),
            static_cast<unsigned long long>(preparedSeen_[0]),static_cast<unsigned long long>(preparedSeen_[1]),
            static_cast<unsigned long long>(drawCandidates_),static_cast<unsigned long long>(queryRefusals_),
            static_cast<unsigned long long>(pipelineUnavailable_),static_cast<unsigned long long>(registry_.ready_count()),
            static_cast<unsigned long long>(c.pairedSubmissions),static_cast<unsigned long long>(c.leftDrawsRemoved),static_cast<unsigned long long>(c.rightDrawsRemoved),
            int(queryKnown_.load()),int(foreign_.load()),active,result,static_cast<unsigned long long>(commandLists_));logger_(line);
    }
    void stop(){
        std::lock_guard<std::recursive_mutex> lock(apiMutex_);
        enabled_=false;group_={};report_stages();queryKnown_=false;registry_.reset();
        const auto c=replacement_?replacement_->counts():StereoNativeReplacementCounts{};if(logger_){char line[384]{};std::snprintf(line,sizeof(line),
            "sps_render native paired=%llu left_removed=%llu right_removed=%llu right_fallback=%llu captured=%llu refused=%llu",
            static_cast<unsigned long long>(c.pairedSubmissions),static_cast<unsigned long long>(c.leftDrawsRemoved),static_cast<unsigned long long>(c.rightDrawsRemoved),
            static_cast<unsigned long long>(c.rightFallbacks),static_cast<unsigned long long>(captures_),static_cast<unsigned long long>(refusals_));logger_(line);}
        {std::lock_guard<std::mutex> queryLock(queryMutex_);queries_.clear();}
        replacement_.reset();pool_.reset();graphicsWrites_.clear();computeWrites_.clear();graphicsValid_=computeValid_=false;
        contextAliases_={};deviceAliases_={};contextAliasCount_=deviceAliasCount_=0;
        context_.reset();device_.reset();frame_=warmed_={};thread_=0;logger_=nullptr;
    }
    StereoNativeReplacementCounts counts()const{return replacement_?replacement_->counts():StereoNativeReplacementCounts{};}
};
inline NativeSpsRuntime nativeSpsRuntime;
class NativeSpsGroupScope {
    native_sps_detail::Group previous_;
public:
    NativeSpsGroupScope(ID3D11DeviceContext* context,uintptr_t renderer,uintptr_t view,unsigned eye,
        const NativeStereoViewInputs* inputs,bool(*readable)(const void*,size_t))
        :previous_(nativeSpsRuntime.begin_group(context,renderer,view,eye,inputs,readable)){}
    ~NativeSpsGroupScope(){nativeSpsRuntime.end_group(std::move(previous_));}
    NativeSpsGroupScope(const NativeSpsGroupScope&)=delete;
};
}
