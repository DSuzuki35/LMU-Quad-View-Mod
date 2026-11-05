#pragma once
#include <d3d11_1.h>
#include "stereo_com_ref.hpp"
#include "xr_frame_inputs.hpp"
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <limits>

namespace lmuqv {
struct StereoConstantAllocation {
    uint64_t generation=0;
    size_t page=size_t(-1);
    UINT first=0,count=0;
    explicit operator bool()const{return page!=size_t(-1);}
};
struct StereoConstantBinding {ID3D11Buffer* buffer=nullptr;UINT first=0,count=0;};
class StereoConstantArena {
    enum class Phase {Empty,Building,Sealed,Submitted,Aborted};
    struct Page {std::vector<uint8_t> bytes;size_t used=0;StereoComRef<ID3D11Buffer> buffer;UINT allocated=0;};
    std::vector<Page> pages_;
    std::unordered_map<std::string,StereoConstantAllocation> deduplicated_;
    StereoComRef<ID3D11Device> device_;
    StereoComRef<ID3D11DeviceContext> context_;
    StereoFrameIdentity identity_;
    Phase phase_=Phase::Empty;
    uint64_t generation_=0;
    size_t active_=0,bytes_=0,budget_=0,maps_=0;
    bool offsets_=false,failed_=false;
    const char* reason_="no constant arena";
public:
    StereoConstantArena()=default;
    StereoConstantArena(const StereoConstantArena&)=delete;
    StereoConstantArena& operator=(const StereoConstantArena&)=delete;
    static constexpr size_t pageBytes=65536,maxBytes=64*1024*1024;
    bool begin(ID3D11DeviceContext* context,const StereoFrameIdentity& key,size_t budget=8*1024*1024,bool forceOrdinaryBuffers=false){
        if((identity_.valid()&&identity_.thread!=GetCurrentThreadId())||!context||!key.valid()||key.thread!=GetCurrentThreadId()||
           context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||budget>maxBytes||phase_==Phase::Building||phase_==Phase::Sealed){reason_="invalid/in-flight constant arena";return false;}
        if(generation_==(std::numeric_limits<uint64_t>::max)()){reason_="constant generation exhausted";return false;}
        StereoComRef<ID3D11Device> device;context->GetDevice(device.put());D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        StereoComRef<ID3D11DeviceContext1> context1;
        const bool offsets=!forceOrdinaryBuffers&&SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof(options)))&&
            options.ConstantBufferOffsetting&&SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(context1.put())));
        if(device_.get()!=device.get()||offsets_!=offsets)pages_.clear();
        device_=std::move(device);context_=StereoComRef<ID3D11DeviceContext>(context);identity_=key;offsets_=offsets;
        for(auto& page:pages_)page.used=0;deduplicated_.clear();active_=bytes_=maps_=0;budget_=budget;failed_=false;
        ++generation_;phase_=Phase::Building;reason_="constant arena open";return true;
    }
    StereoConstantAllocation add(const std::vector<uint8_t>& payload) noexcept {
        StereoConstantAllocation allocation;
        if(phase_!=Phase::Building||identity_.thread!=GetCurrentThreadId()||failed_){reason_="constant arena not writable";return allocation;}
        if(payload.empty()||payload.size()>pageBytes||payload.size()%16){reason_="invalid owned constant payload";failed_=true;return allocation;}
        try {
            const std::string key(reinterpret_cast<const char*>(payload.data()),payload.size());
            const auto found=deduplicated_.find(key);if(found!=deduplicated_.end())return found->second;
            const size_t padded=offsets_?(payload.size()+255)/256*256:payload.size();
            if(padded>budget_-bytes_){reason_="constant arena byte budget";failed_=true;return allocation;}
            size_t index=0;
            if(offsets_&&active_&&pages_[active_-1].used+padded<=pageBytes)index=active_-1;
            else {index=active_++;if(index==pages_.size())pages_.emplace_back();pages_[index].bytes.resize(offsets_?pageBytes:padded);}
            auto& page=pages_[index];allocation={generation_,index,UINT(page.used/16),UINT(padded/16)};
            std::memcpy(page.bytes.data()+page.used,payload.data(),payload.size());
            if(padded>payload.size())std::memset(page.bytes.data()+page.used+payload.size(),0,padded-payload.size());
            page.used+=padded;bytes_+=padded;deduplicated_.emplace(key,allocation);return allocation;
        }catch(...){reason_="constant arena allocation failure";failed_=true;return {};}
    }
    bool seal() noexcept {
        if(phase_!=Phase::Building||failed_||identity_.thread!=GetCurrentThreadId())return false;
        for(size_t i=0;i<active_;++i){auto& page=pages_[i];const UINT capacity=UINT(offsets_?pageBytes:page.used);
            if(!page.buffer||page.allocated!=capacity){D3D11_BUFFER_DESC desc{};desc.ByteWidth=capacity;desc.Usage=D3D11_USAGE_DYNAMIC;
                desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
                if(FAILED(device_->CreateBuffer(&desc,nullptr,page.buffer.put()))){reason_="constant arena GPU allocation failed";failed_=true;return false;}page.allocated=capacity;}
            D3D11_MAPPED_SUBRESOURCE map{};const auto hr=context_->Map(page.buffer.get(),0,D3D11_MAP_WRITE_DISCARD,0,&map);
            if(FAILED(hr)||!map.pData){if(SUCCEEDED(hr))context_->Unmap(page.buffer.get(),0);reason_="constant arena write map failed";failed_=true;return false;}
            // WRITE mappings are written only. Never inspect mapped contents.
            std::memcpy(map.pData,page.bytes.data(),page.used);context_->Unmap(page.buffer.get(),0);++maps_;
        }
        phase_=Phase::Sealed;reason_="constant pages uploaded";return true;
    }
    StereoConstantBinding resolve(const StereoConstantAllocation& allocation)const{
        if((phase_!=Phase::Sealed&&phase_!=Phase::Submitted)||allocation.generation!=generation_||allocation.page>=active_||identity_.thread!=GetCurrentThreadId())return {};
        const auto& page=pages_[allocation.page];
        if(!allocation.count||(uint64_t(allocation.first)+allocation.count)*16>page.used||
           (offsets_&&(allocation.first%16||allocation.count%16))||(!offsets_&&allocation.first))return {};
        return {page.buffer.get(),allocation.first,allocation.count};
    }
    bool mark_submitted(const StereoFrameIdentity& key){if(phase_!=Phase::Sealed||identity_!=key||key.thread!=GetCurrentThreadId())return false;phase_=Phase::Submitted;return true;}
    bool ready(const StereoFrameIdentity& key,uint64_t generation)const{return phase_==Phase::Sealed&&identity_==key&&generation_==generation&&key.thread==GetCurrentThreadId();}
    void abort(){if(!identity_.valid()||identity_.thread==GetCurrentThreadId())phase_=Phase::Aborted;}
    bool uses_offsets()const{return offsets_;}
    uint64_t generation()const{return generation_;}
    size_t map_count()const{return maps_;}size_t packed_bytes()const{return bytes_;}
    const char* reason()const{return reason_;}
};
}
