#pragma once
#include "stereo_com_ref.hpp"
#include "stereo_instancing.hpp"
#include "stereo_shader.hpp"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <memory>
#include <unordered_map>

namespace lmuqv {
struct StereoInputElement {
    std::string semantic;
    UINT index=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    UINT slot=0,offset=0;
    D3D11_INPUT_CLASSIFICATION classification=D3D11_INPUT_PER_VERTEX_DATA;
    UINT step=0;
};
// Constructed only by the cache, then immutable. Preparation owns every shader
// and semantic string; no driver compilation is performed during draw replay.
class StereoPipelineCache;
class StereoPipeline {
    friend class StereoPipelineCache;
    StereoPipeline()=default;
public:
    StereoComRef<ID3D11Device> device;
    StereoComRef<ID3D11VertexShader> monoVs,pairedVs;
    StereoComRef<ID3D11PixelShader> monoPs,pairedPs;
    StereoComRef<ID3D11GeometryShader> geometry;
    StereoComRef<ID3D11InputLayout> monoLayout,pairedLayout;
    StereoShaderVariant vertex,pixel;
    std::vector<StereoInputElement> elements;
    uint32_t inputSlots=0;
    std::array<std::array<uint32_t,4>,2> resourceSlots{};
    bool direct=false;
};
class StereoPipelineCache {
    StereoComRef<ID3D11Device> device_;
    uint32_t thread_=0;
    bool direct_=false;
    size_t keyBytes_=0;
    std::unordered_map<std::string,std::shared_ptr<const StereoPipeline>> programs_;
    std::string reason_="no pipeline device";
public:
    bool configure(ID3D11Device* device,bool forceGeometry=false){
        if(thread_&&thread_!=GetCurrentThreadId()){reason_="foreign pipeline thread";return false;}
        const auto caps=query_stereo_capabilities(device);
        if(caps.route==StereoRoute::unavailable){reason_="stereo shaders need feature level 11";return false;}
        const bool direct=caps.vertexArrayIndex&&!forceGeometry;
        if(device_.get()!=device||direct_!=direct){programs_.clear();keyBytes_=0;}
        device_=StereoComRef<ID3D11Device>(device);thread_=GetCurrentThreadId();direct_=direct;return true;
    }
    std::shared_ptr<const StereoPipeline> get(const std::vector<uint8_t>& vs,const std::vector<uint8_t>& ps,
        uint32_t vertexSlots,uint32_t pixelSlots,const std::vector<StereoInputElement>& elements) noexcept {
        if(!device_||thread_!=GetCurrentThreadId()){reason_="invalid pipeline scope/thread";return {};}
        if(vs.empty()||ps.empty()||vs.size()>512*1024||ps.size()>512*1024||elements.empty()||elements.size()>32){reason_="shader/layout limits";return {};}
        try {
            std::string key;key.reserve(vs.size()+ps.size()+elements.size()*96+32);
            const auto scalar=[&](uint32_t v){key.append(reinterpret_cast<const char*>(&v),sizeof(v));};
            scalar(UINT(vs.size()));key.append(reinterpret_cast<const char*>(vs.data()),vs.size());
            scalar(UINT(ps.size()));key.append(reinterpret_cast<const char*>(ps.data()),ps.size());scalar(vertexSlots);scalar(pixelSlots);
            scalar(UINT(elements.size()));
            for(const auto& e:elements){
                if(e.semantic.empty()||e.semantic.size()>64||e.semantic.find('\0')!=std::string::npos||e.slot>=32||
                   (e.classification!=D3D11_INPUT_PER_VERTEX_DATA&&e.classification!=D3D11_INPUT_PER_INSTANCE_DATA)||
                   (e.classification==D3D11_INPUT_PER_VERTEX_DATA&&e.step)||
                   (e.classification==D3D11_INPUT_PER_INSTANCE_DATA&&e.step>(std::numeric_limits<UINT>::max)()/2)){
                    reason_="unsupported input element";return {};}
                scalar(UINT(e.semantic.size()));key+=e.semantic;scalar(e.index);scalar(e.format);scalar(e.slot);scalar(e.offset);scalar(e.classification);scalar(e.step);
            }
            const auto found=programs_.find(key);if(found!=programs_.end())return found->second;
            if(programs_.size()>=512||key.size()>64*1024*1024-keyBytes_){reason_="pipeline cache budget";return {};}
            auto p=std::shared_ptr<StereoPipeline>(new StereoPipeline);p->device=device_;p->direct=direct_;p->elements=elements;
            if(!convert_stereo_shader(vs.data(),vs.size(),StereoShaderStage::vertex,vertexSlots,direct_,p->vertex,reason_)||
               !convert_stereo_shader(ps.data(),ps.size(),StereoShaderStage::pixel,pixelSlots,false,p->pixel,reason_))return {};
            for(unsigned stage=0;stage<2;++stage){const auto& source=stage?ps:vs;StereoComRef<ID3D11ShaderReflection> reflection;
                if(FAILED(D3DReflect(source.data(),source.size(),IID_ID3D11ShaderReflection,reinterpret_cast<void**>(reflection.put())))){
                    reason_="shader binding reflection unavailable";return {};}
                D3D11_SHADER_DESC desc{};if(FAILED(reflection->GetDesc(&desc))){reason_="shader reflection failed";return {};}
                if(stage)for(UINT i=0;i<desc.OutputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC output{};
                    if(FAILED(reflection->GetOutputParameterDesc(i,&output))||
                       (output.SystemValueType!=D3D_NAME_DEPTH&&output.SystemValueType!=D3D_NAME_DEPTH_GREATER_EQUAL&&
                        output.SystemValueType!=D3D_NAME_DEPTH_LESS_EQUAL&&
                        (output.SystemValueType!=D3D_NAME_TARGET||output.SemanticIndex))){reason_="pixel shader requires an unsupported output target";return {};}}
                for(UINT i=0;i<desc.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};
                    if(FAILED(reflection->GetResourceBindingDesc(i,&b))){reason_="shader binding reflection failed";return {};}
                    if(b.Type==D3D_SIT_CBUFFER||b.Type==D3D_SIT_SAMPLER)continue;
                    if((b.Type!=D3D_SIT_TEXTURE&&b.Type!=D3D_SIT_STRUCTURED&&b.Type!=D3D_SIT_BYTEADDRESS)||
                       b.BindPoint>=128||!b.BindCount||b.BindCount>128-b.BindPoint){reason_="unsupported shader resource binding";return {};}
                    for(UINT j=0;j<b.BindCount;++j)p->resourceSlots[stage][(b.BindPoint+j)/32]|=1u<<((b.BindPoint+j)%32);
                }
            }
            if(FAILED(device_->CreateVertexShader(vs.data(),vs.size(),nullptr,p->monoVs.put()))||
               FAILED(device_->CreatePixelShader(ps.data(),ps.size(),nullptr,p->monoPs.put()))||
               FAILED(device_->CreateVertexShader(p->vertex.bytecode.data(),p->vertex.bytecode.size(),nullptr,p->pairedVs.put()))||
               FAILED(device_->CreatePixelShader(p->pixel.bytecode.data(),p->pixel.bytecode.size(),nullptr,p->pairedPs.put()))){reason_="shader object creation failed";return {};}
            std::vector<D3D11_INPUT_ELEMENT_DESC> mono,paired;
            for(const auto& e:p->elements){D3D11_INPUT_ELEMENT_DESC d{e.semantic.c_str(),e.index,e.format,e.slot,e.offset,e.classification,e.step};
                mono.push_back(d);if(e.classification==D3D11_INPUT_PER_INSTANCE_DATA)d.InstanceDataStepRate*=2;
                paired.push_back(d);p->inputSlots|=1u<<e.slot;}
            if(FAILED(device_->CreateInputLayout(mono.data(),UINT(mono.size()),vs.data(),vs.size(),p->monoLayout.put()))||
               FAILED(device_->CreateInputLayout(paired.data(),UINT(paired.size()),p->vertex.bytecode.data(),p->vertex.bytecode.size(),p->pairedLayout.put()))){reason_="input layout creation failed";return {};}
            if(!direct_){StereoComRef<ID3DBlob> code,errors;
                if(FAILED(D3DCompile(p->vertex.geometrySource.data(),p->vertex.geometrySource.size(),nullptr,nullptr,nullptr,"main","gs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.put(),errors.put()))||
                   FAILED(device_->CreateGeometryShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,p->geometry.put()))){reason_="geometry routing shader creation failed";return {};}
            }
            const auto bytes=key.size();programs_.emplace(std::move(key),p);keyBytes_+=bytes;reason_="cached stereo pipeline";return p;
        }catch(...){reason_="pipeline preparation allocation failure";return {};}
    }
    size_t size()const{return programs_.size();}
    const std::string& reason()const{return reason_;}
};
}
