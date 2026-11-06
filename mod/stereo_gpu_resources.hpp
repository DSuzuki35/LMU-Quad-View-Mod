#pragma once
#include <d3d11.h>
#include "stereo_com_ref.hpp"
#include "xr_frame_inputs.hpp"
#include <memory>
#include <unordered_map>
#include <algorithm>

namespace lmuqv {
namespace stereo_gpu_detail {
inline bool texture_bytes(const D3D11_TEXTURE2D_DESC& d,uint64_t& bytes){
    unsigned block=1,unit=0;
    switch(d.Format){
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:case DXGI_FORMAT_R32G32B32A32_FLOAT:case DXGI_FORMAT_R32G32B32A32_UINT:case DXGI_FORMAT_R32G32B32A32_SINT:unit=16;break;
    case DXGI_FORMAT_R32G32B32_TYPELESS:case DXGI_FORMAT_R32G32B32_FLOAT:case DXGI_FORMAT_R32G32B32_UINT:case DXGI_FORMAT_R32G32B32_SINT:unit=12;break;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:case DXGI_FORMAT_R16G16B16A16_FLOAT:case DXGI_FORMAT_R16G16B16A16_UNORM:case DXGI_FORMAT_R16G16B16A16_UINT:case DXGI_FORMAT_R16G16B16A16_SNORM:case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:case DXGI_FORMAT_R32G32_FLOAT:case DXGI_FORMAT_R32G32_UINT:case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:unit=8;break;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:case DXGI_FORMAT_R10G10B10A2_UNORM:case DXGI_FORMAT_R10G10B10A2_UINT:case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:case DXGI_FORMAT_R8G8B8A8_UINT:case DXGI_FORMAT_R8G8B8A8_SNORM:case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:case DXGI_FORMAT_B8G8R8X8_TYPELESS:case DXGI_FORMAT_B8G8R8X8_UNORM:case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16_TYPELESS:case DXGI_FORMAT_R16G16_FLOAT:case DXGI_FORMAT_R16G16_UNORM:case DXGI_FORMAT_R16G16_UINT:case DXGI_FORMAT_R16G16_SNORM:case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R32_UINT:case DXGI_FORMAT_R32_SINT:case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:case DXGI_FORMAT_X24_TYPELESS_G8_UINT:unit=4;break;
    case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_R16_FLOAT:case DXGI_FORMAT_R16_UNORM:case DXGI_FORMAT_R16_UINT:case DXGI_FORMAT_R16_SNORM:case DXGI_FORMAT_R16_SINT:case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R8G8_TYPELESS:case DXGI_FORMAT_R8G8_UNORM:case DXGI_FORMAT_R8G8_UINT:case DXGI_FORMAT_R8G8_SNORM:case DXGI_FORMAT_R8G8_SINT:unit=2;break;
    case DXGI_FORMAT_R8_TYPELESS:case DXGI_FORMAT_R8_UNORM:case DXGI_FORMAT_R8_UINT:case DXGI_FORMAT_R8_SNORM:case DXGI_FORMAT_R8_SINT:case DXGI_FORMAT_A8_UNORM:unit=1;break;
    case DXGI_FORMAT_BC1_TYPELESS:case DXGI_FORMAT_BC1_UNORM:case DXGI_FORMAT_BC1_UNORM_SRGB:case DXGI_FORMAT_BC4_TYPELESS:case DXGI_FORMAT_BC4_UNORM:case DXGI_FORMAT_BC4_SNORM:block=4;unit=8;break;
    case DXGI_FORMAT_BC2_TYPELESS:case DXGI_FORMAT_BC2_UNORM:case DXGI_FORMAT_BC2_UNORM_SRGB:case DXGI_FORMAT_BC3_TYPELESS:case DXGI_FORMAT_BC3_UNORM:case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS:case DXGI_FORMAT_BC5_UNORM:case DXGI_FORMAT_BC5_SNORM:case DXGI_FORMAT_BC6H_TYPELESS:case DXGI_FORMAT_BC6H_UF16:case DXGI_FORMAT_BC6H_SF16:case DXGI_FORMAT_BC7_TYPELESS:case DXGI_FORMAT_BC7_UNORM:case DXGI_FORMAT_BC7_UNORM_SRGB:block=4;unit=16;break;
    default:return false;
    }
    if(!d.Width||!d.Height||d.Width>16384||d.Height>16384||!d.MipLevels||d.MipLevels>15||!d.ArraySize||d.ArraySize>2048||!d.SampleDesc.Count||d.SampleDesc.Count>32)return false;
    bytes=0;unsigned w=d.Width,h=d.Height;
    for(unsigned mip=0;mip<d.MipLevels;++mip){bytes+=uint64_t((w+block-1)/block)*((h+block-1)/block)*unit*d.ArraySize*d.SampleDesc.Count;w=(std::max)(1u,w/2);h=(std::max)(1u,h/2);}
    return true;
}
struct FrozenResource {
    StereoComRef<ID3D11Resource> resource,source;
    StereoComRef<ID3D11Device> device;
    D3D11_RESOURCE_DIMENSION dimension=D3D11_RESOURCE_DIMENSION_UNKNOWN;
    D3D11_BUFFER_DESC buffer{};
    uint64_t bytes=0;
};
}
class StereoGpuResourcePool;
class StereoResourceLease {
    friend class StereoGpuResourcePool;
    std::shared_ptr<const stereo_gpu_detail::FrozenResource> frozen_;
    StereoFrameIdentity identity_;
public:
    explicit operator bool()const{return bool(frozen_);}
    ID3D11Resource* resource()const{return frozen_?frozen_->resource.get():nullptr;}
    ID3D11Resource* source_resource()const{return frozen_?frozen_->source.get():nullptr;}
    ID3D11Device* resource_device()const{return frozen_?frozen_->device.get():nullptr;}
    const D3D11_BUFFER_DESC* buffer_description()const{return frozen_&&frozen_->dimension==D3D11_RESOURCE_DIMENSION_BUFFER?&frozen_->buffer:nullptr;}
    const StereoFrameIdentity& identity()const{return identity_;}
    bool same_contents(const StereoResourceLease& other)const{return frozen_&&frozen_==other.frozen_;}
};
class StereoShaderResourceLease {
    friend class StereoGpuResourcePool;
    StereoResourceLease resource_;
    StereoComRef<ID3D11ShaderResourceView> view_;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc_{};
public:
    explicit operator bool()const{return bool(resource_)&&bool(view_);}
    ID3D11ShaderResourceView* view()const{return view_.get();}
    ID3D11Resource* source_resource()const{return resource_.source_resource();}
    ID3D11Device* resource_device()const{return resource_.resource_device();}
    const StereoFrameIdentity& identity()const{return resource_.identity();}
    bool same_binding(const StereoShaderResourceLease& other)const{
        return resource_.same_contents(other.resource_)&&!std::memcmp(&desc_,&other.desc_,sizeof(desc_));
    }
};
class StereoGpuResourcePool {
    StereoComRef<ID3D11Device> device_;
    StereoComRef<ID3D11DeviceContext> context_;
    StereoFrameIdentity identity_;
    uint64_t budget_=0,copied_=0;
    size_t created_=0;
    std::unordered_map<ID3D11Resource*,std::weak_ptr<const stereo_gpu_detail::FrozenResource>> immutable_;
    const char* reason_="no resource scope";
public:
    static constexpr uint64_t defaultByteBudget=256ull*1024*1024;
    bool begin(ID3D11DeviceContext* context,const StereoFrameIdentity& identity,uint64_t byteBudget=defaultByteBudget){
        if(identity_.valid()&&identity_.thread!=GetCurrentThreadId()){reason_="foreign resource scope thread";return false;}
        auto previousDevice=device_;
        context_.reset();device_.reset();identity_={};copied_=0;created_=0;
        if(!context||!identity.valid()||identity.thread!=GetCurrentThreadId()||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||byteBudget>defaultByteBudget){reason_="invalid resource scope";return false;}
        StereoComRef<ID3D11Device> device;context->GetDevice(device.put());
        if(!device||device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0||FAILED(device->GetDeviceRemovedReason())){reason_="resource device unavailable";return false;}
        if(previousDevice.get()!=device.get())immutable_.clear();
        for(auto i=immutable_.begin();i!=immutable_.end();)if(i->second.expired())i=immutable_.erase(i);else ++i;
        device_=std::move(device);context_=StereoComRef<ID3D11DeviceContext>(context);identity_=identity;budget_=byteBudget;reason_="resource scope open";return true;
    }
    StereoResourceLease freeze(ID3D11Resource* source) noexcept {
        StereoResourceLease lease;
        if(!context_||identity_.thread!=GetCurrentThreadId()||!source){reason_="invalid resource/thread";return lease;}
        StereoComRef<ID3D11Device> sourceDevice;source->GetDevice(sourceDevice.put());
        if(sourceDevice.get()!=device_.get()){reason_="foreign resource device";return lease;}
        try {
            D3D11_RESOURCE_DIMENSION dimension{};source->GetType(&dimension);D3D11_USAGE usage{};uint64_t bytes=0;
            D3D11_BUFFER_DESC buffer{};D3D11_TEXTURE2D_DESC texture{};
            StereoComRef<ID3D11Buffer> sourceBuffer;StereoComRef<ID3D11Texture2D> sourceTexture;
            if(dimension==D3D11_RESOURCE_DIMENSION_BUFFER){
                if(FAILED(source->QueryInterface(__uuidof(ID3D11Buffer),reinterpret_cast<void**>(sourceBuffer.put())))){reason_="buffer interface unavailable";return lease;}
                sourceBuffer->GetDesc(&buffer);usage=buffer.Usage;bytes=buffer.ByteWidth;
                if(!bytes||buffer.CPUAccessFlags||(buffer.MiscFlags&~(D3D11_RESOURCE_MISC_BUFFER_STRUCTURED|D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS))){reason_="unsupported/mappable buffer";return lease;}
            }else if(dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D){
                if(FAILED(source->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(sourceTexture.put())))){reason_="texture interface unavailable";return lease;}
                sourceTexture->GetDesc(&texture);usage=texture.Usage;
                if(texture.CPUAccessFlags||(texture.MiscFlags&~(D3D11_RESOURCE_MISC_GENERATE_MIPS|D3D11_RESOURCE_MISC_TEXTURECUBE))||!stereo_gpu_detail::texture_bytes(texture,bytes)){
                    reason_="unsupported/mappable texture";return lease;}
            }else {reason_="unsupported resource dimension";return lease;}
            if(usage!=D3D11_USAGE_DEFAULT&&usage!=D3D11_USAGE_IMMUTABLE){reason_="resource requires verified unmapped producer";return lease;}
            if(usage==D3D11_USAGE_IMMUTABLE){const auto found=immutable_.find(source);if(found!=immutable_.end())if(auto frozen=found->second.lock()){
                lease.frozen_=std::move(frozen);lease.identity_=identity_;reason_="retained immutable contents";return lease;}}
            if(created_>=4096||(usage!=D3D11_USAGE_IMMUTABLE&&bytes>budget_-copied_)){reason_="GPU snapshot budget";return lease;}
            auto frozen=std::make_shared<stereo_gpu_detail::FrozenResource>();frozen->bytes=bytes;frozen->source=StereoComRef<ID3D11Resource>(source);
            frozen->device=device_;frozen->dimension=dimension;
            if(usage==D3D11_USAGE_IMMUTABLE){frozen->resource=StereoComRef<ID3D11Resource>(source);immutable_[source]=frozen;}
            else {
                HRESULT hr=E_FAIL;
                if(sourceBuffer){buffer.Usage=D3D11_USAGE_DEFAULT;buffer.CPUAccessFlags=0;StereoComRef<ID3D11Buffer> copy;
                    hr=device_->CreateBuffer(&buffer,nullptr,copy.put());if(SUCCEEDED(hr))frozen->resource=StereoComRef<ID3D11Resource>(copy.get());}
                else {texture.Usage=D3D11_USAGE_DEFAULT;texture.CPUAccessFlags=0;StereoComRef<ID3D11Texture2D> copy;
                    hr=device_->CreateTexture2D(&texture,nullptr,copy.put());if(SUCCEEDED(hr))frozen->resource=StereoComRef<ID3D11Resource>(copy.get());}
                if(FAILED(hr)){reason_="GPU snapshot allocation failed";return lease;}
                context_->CopyResource(frozen->resource.get(),source);copied_+=bytes;
            }
            frozen->buffer=buffer;++created_;lease.frozen_=std::move(frozen);lease.identity_=identity_;reason_="owned GPU content snapshot";return lease;
        }catch(...){reason_="GPU snapshot bookkeeping allocation failed";return lease;}
    }
    // Reuse an already-owned immutable content snapshot, not its mutable
    // source. Asset changes create a fresh snapshot; unchanged assets need no
    // resource copy, allocation or driver descriptor query in another frame.
    StereoResourceLease pin(const StereoResourceLease& snapshot) noexcept {
        StereoResourceLease result;
        if(!context_||identity_.thread!=GetCurrentThreadId()||!snapshot||snapshot.resource_device()!=device_.get()){
            reason_="invalid SPS asset snapshot scope";return result;}
        result.frozen_=snapshot.frozen_;result.identity_=identity_;return result;
    }
    StereoShaderResourceLease pin(const StereoShaderResourceLease& snapshot) noexcept {
        StereoShaderResourceLease result;if(!snapshot)return result;
        auto resource=pin(snapshot.resource_);if(!resource)return result;
        result.resource_=std::move(resource);result.view_=snapshot.view_;result.desc_=snapshot.desc_;return result;
    }
    bool stamp(StereoResourceLease& snapshot) noexcept {
        if(!context_||identity_.thread!=GetCurrentThreadId()||!snapshot||snapshot.resource_device()!=device_.get())return false;
        snapshot.identity_=identity_;return true;
    }
    bool stamp(StereoShaderResourceLease& snapshot) noexcept{return bool(snapshot)&&stamp(snapshot.resource_);}
    bool create_view(const StereoResourceLease& lease,const D3D11_SHADER_RESOURCE_VIEW_DESC& desc,StereoComRef<ID3D11ShaderResourceView>& out) noexcept {
        out.reset();if(!lease||lease.identity()!=identity_||identity_.thread!=GetCurrentThreadId()||!device_){reason_="stale resource view lease";return false;}
        if(FAILED(device_->CreateShaderResourceView(lease.resource(),&desc,out.put()))){reason_="frozen resource view creation failed";return false;}return true;
    }
    StereoShaderResourceLease create_shader_view(const StereoResourceLease& lease,const D3D11_SHADER_RESOURCE_VIEW_DESC& desc) noexcept {
        StereoShaderResourceLease out;if(!create_view(lease,desc,out.view_))return out;
        out.resource_=lease;out.view_->GetDesc(&out.desc_);return out;
    }
    uint64_t copied_bytes()const{return copied_;}
    ID3D11Device* device()const{return device_.get();}
    bool matches(ID3D11DeviceContext* context,const StereoFrameIdentity& key)const{
        return context_.get()==context&&identity_==key&&key.valid()&&key.thread==GetCurrentThreadId();
    }
    size_t snapshots_created()const{return created_;}
    const char* reason()const{return reason_;}
};
}
