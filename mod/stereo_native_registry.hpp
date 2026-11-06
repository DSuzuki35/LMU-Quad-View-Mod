#pragma once
#include "stereo_pipeline.hpp"
#include <deque>
#include <mutex>
#include <tuple>

namespace lmuqv {
struct StereoNativePipelineKey {
    ID3D11VertexShader* vertex=nullptr;ID3D11PixelShader* pixel=nullptr;ID3D11InputLayout* layout=nullptr;
    bool operator==(const StereoNativePipelineKey& other)const{return vertex==other.vertex&&pixel==other.pixel&&layout==other.layout;}
};
struct StereoNativePipelineKeyHash {size_t operator()(const StereoNativePipelineKey& key)const{
    return std::hash<void*>{}(key.vertex)^(std::hash<void*>{}(key.pixel)<<1)^(std::hash<void*>{}(key.layout)<<2);
}};
// Creation metadata only. No per-draw reflection or shader compilation. A draw
// may request an observed triple; warming happens later outside native draws.
class StereoNativeRegistry {
    struct Program {std::vector<uint8_t> code;uint32_t constants=0;bool vertex=false;};
    struct Shader {StereoComRef<ID3D11DeviceChild> object;std::shared_ptr<const Program> program;};
    struct Layout {StereoComRef<ID3D11InputLayout> object;std::vector<StereoInputElement> elements;};
    std::mutex mutex_;
    std::unordered_map<std::string,std::shared_ptr<const Program>> programs_;
    std::unordered_map<ID3D11DeviceChild*,Shader> shaders_;
    std::unordered_map<ID3D11InputLayout*,Layout> layouts_;
    std::unordered_map<StereoNativePipelineKey,std::shared_ptr<const StereoPipeline>,StereoNativePipelineKeyHash> ready_;
    std::unordered_map<std::string,std::shared_ptr<const StereoPipeline>> assetReady_;
    std::deque<StereoNativePipelineKey> pending_;
    StereoPipelineCache cache_;
    uint64_t bytes_=0;
    uint32_t thread_=0;
    bool configured_=false;
public:
    // Asset preparation has no native draw to fall back to and no later eye
    // verification/warm-up gate. Resolve creation metadata synchronously here,
    // outside submission; the resulting pipeline is immutable and cached.
    std::shared_ptr<const StereoPipeline> resolve_asset(ID3D11Device* device,const StereoNativePipelineKey& key,
        uint32_t vertexViewSlots,uint32_t pixelViewSlots,bool forceGeometry=false) noexcept {
        if(!device||(thread_&&thread_!=GetCurrentThreadId()))return {};
        try{
            std::string identity;identity.append(reinterpret_cast<const char*>(&key.vertex),sizeof(key.vertex));
            identity.append(reinterpret_cast<const char*>(&key.pixel),sizeof(key.pixel));identity.append(reinterpret_cast<const char*>(&key.layout),sizeof(key.layout));
            identity.append(reinterpret_cast<const char*>(&vertexViewSlots),sizeof(vertexViewSlots));identity.append(reinterpret_cast<const char*>(&pixelViewSlots),sizeof(pixelViewSlots));
            identity.push_back(forceGeometry?'g':'v');
            std::shared_ptr<const Program> vertex,pixel;std::vector<StereoInputElement> elements;
            {std::lock_guard<std::mutex> lock(mutex_);const auto ready=assetReady_.find(identity);if(ready!=assetReady_.end())return ready->second;
                const auto vs=shaders_.find(key.vertex),ps=shaders_.find(key.pixel);const auto layout=layouts_.find(key.layout);
                if(vs==shaders_.end()||ps==shaders_.end()||layout==layouts_.end()||!vs->second.program->vertex||ps->second.program->vertex)return {};
                vertex=vs->second.program;pixel=ps->second.program;elements=layout->second.elements;}
            thread_=GetCurrentThreadId();if(!configured_){if(!cache_.configure(device,forceGeometry))return {};configured_=true;}
            auto pipeline=cache_.get(vertex->code,pixel->code,vertexViewSlots&vertex->constants,pixelViewSlots&pixel->constants,elements);
            std::lock_guard<std::mutex> lock(mutex_);assetReady_.emplace(std::move(identity),pipeline);return pipeline;
        }catch(...){return {};}
    }
    void shader(ID3D11DeviceChild* object,const void* code,size_t bytes,bool vertex) noexcept {
        if(!object||!code||!bytes||bytes>512*1024)return;
        try {
            std::lock_guard<std::mutex> lock(mutex_);if(shaders_.count(object)||shaders_.size()>=32768)return;
            std::string key(1,vertex?'v':'p');key.append(static_cast<const char*>(code),bytes);
            auto found=programs_.find(key);std::shared_ptr<const Program> program;
            if(found!=programs_.end())program=found->second;
            else{
                if(programs_.size()>=8192||bytes>32*1024*1024-bytes_)return;
                StereoComRef<ID3D11ShaderReflection> reflection;
                if(FAILED(D3DReflect(code,bytes,IID_ID3D11ShaderReflection,reinterpret_cast<void**>(reflection.put()))))return;
                D3D11_SHADER_DESC desc{};if(FAILED(reflection->GetDesc(&desc)))return;
                auto value=std::make_shared<Program>();value->vertex=vertex;
                for(UINT i=0;i<desc.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC binding{};
                    if(FAILED(reflection->GetResourceBindingDesc(i,&binding)))return;
                    if(binding.Type==D3D_SIT_CBUFFER){if(binding.BindPoint>=13||binding.BindCount!=1)return;value->constants|=1u<<binding.BindPoint;}}
                const auto* begin=static_cast<const uint8_t*>(code);value->code.assign(begin,begin+bytes);
                program=value;programs_.emplace(std::move(key),program);bytes_+=bytes;
            }
            shaders_.emplace(object,Shader{StereoComRef<ID3D11DeviceChild>(object),std::move(program)});
        }catch(...){}
    }
    void layout(ID3D11InputLayout* object,const D3D11_INPUT_ELEMENT_DESC* elements,UINT count) noexcept {
        if(!object||!elements||!count||count>32)return;
        try {
            std::lock_guard<std::mutex> lock(mutex_);if(layouts_.count(object)||layouts_.size()>=8192)return;
            Layout value;value.object=StereoComRef<ID3D11InputLayout>(object);value.elements.reserve(count);
            for(UINT i=0;i<count;++i){const auto& e=elements[i];if(!e.SemanticName)return;
                size_t length=0;while(length<=64&&e.SemanticName[length])++length;if(!length||length>64)return;
                value.elements.push_back({std::string(e.SemanticName,length),e.SemanticIndex,e.Format,e.InputSlot,e.AlignedByteOffset,e.InputSlotClass,e.InstanceDataStepRate});}
            layouts_.emplace(object,std::move(value));
        }catch(...){}
    }
    std::shared_ptr<const StereoPipeline> request(const StereoNativePipelineKey& key) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);const auto ready=ready_.find(key);if(ready!=ready_.end())return ready->second;
            const auto vs=shaders_.find(key.vertex),ps=shaders_.find(key.pixel);
            if(vs==shaders_.end()||ps==shaders_.end()||layouts_.find(key.layout)==layouts_.end()||
               !vs->second.program->vertex||ps->second.program->vertex||ready_.size()>=32768)return {};
            if(pending_.size()<64&&std::find(pending_.begin(),pending_.end(),key)==pending_.end())pending_.push_back(key);
        }catch(...){}
        return {};
    }
    bool warm_one(ID3D11Device* device,bool forceGeometry=false) noexcept {
        if(!device||(thread_&&thread_!=GetCurrentThreadId()))return false;
        try {
            StereoNativePipelineKey key;std::shared_ptr<const Program> vertex,pixel;std::vector<StereoInputElement> elements;
            {
                std::lock_guard<std::mutex> lock(mutex_);if(pending_.empty())return false;
                key=pending_.front();pending_.pop_front();vertex=shaders_.at(key.vertex).program;pixel=shaders_.at(key.pixel).program;elements=layouts_.at(key.layout).elements;
            }
            thread_=GetCurrentThreadId();if(!configured_){if(!cache_.configure(device,forceGeometry))return false;configured_=true;}
            auto pipeline=cache_.get(vertex->code,pixel->code,vertex->constants,pixel->constants,elements);
            std::lock_guard<std::mutex> lock(mutex_);ready_.emplace(key,pipeline);return bool(pipeline);
        }catch(...){return false;}
    }
    size_t ready_count(){std::lock_guard<std::mutex> lock(mutex_);size_t count=0;for(const auto& p:ready_)count+=bool(p.second);return count;}
    void reset(){std::lock_guard<std::mutex> lock(mutex_);pending_.clear();ready_.clear();assetReady_.clear();layouts_.clear();shaders_.clear();programs_.clear();cache_=StereoPipelineCache{};bytes_=0;configured_=false;thread_=0;}
};
}
