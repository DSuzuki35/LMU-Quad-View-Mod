#pragma once
#include "native_outer_texture.hpp"
#include "native_outer_bindings.hpp"
#include <array>
#include <cstdio>
#include <cstring>

namespace lmuqv {
// Exact pinned-build native ABI. Native wrappers and containers are not COM
// objects and are never shallow-copied or freed by this module's allocator.
class NativeOuterBank {
public:
    enum Role:unsigned {Hdr,Depth,Eye0,Eye1,Resolve,DepthResolve,Motion,Upscale,Count};
    using Readable=bool(*)(const void*,std::size_t);
    using Log=void(*)(const char*);
private:
    struct Entry {void** slot{};void* original{};void* wrapper{};void* source{};void* associated{};NativeOuterTexture clone;};
    std::array<Entry,Count> entries_{};
    std::array<Entry,Count> auxiliary_{};
    unsigned auxiliaryCount_=0;
    std::uintptr_t base_=0,renderer_=0;
    DWORD owner_=0;
    unsigned width_=0,height_=0;
    bool ready_=false;
    std::uint64_t generation_=0;
    template<class T>static T at(const void* object,std::size_t offset){
        T value{};std::memcpy(&value,static_cast<const std::byte*>(object)+offset,sizeof(value));return value;
    }
    template<class Fn>Fn fn(std::uintptr_t rva)const{return reinterpret_cast<Fn>(base_+rva);}
    bool vectorFirst(void* container,std::size_t offset,void*& value,Readable read){
        const auto begin=at<std::uintptr_t>(container,offset),end=at<std::uintptr_t>(container,offset+8);
        if(end<begin||end-begin>8||(end-begin)%8)return false;
        value=nullptr;
        if(end!=begin){if(!read(reinterpret_cast<void*>(begin),8))return false;value=*reinterpret_cast<void**>(begin);}
        return true;
    }
    bool cloneEntry(Entry& e,unsigned index,Readable read,Log log){
        if(!e.original)return true;
        auto reject=[&](const char* stage){
            char message[192]{};std::snprintf(message,sizeof(message),
                "native_outer rejected role=%u stage=%s requested=%ux%u",index,stage,width_,height_);
            log(message);return false;
        };
        if(!read(e.original,0xb0)||at<unsigned>(e.original,0)&0x1000u)return reject("wrapper");
        e.associated=at<void*>(e.original,0xa8);
        void* container=at<void*>(e.original,0xa0);
        if(!read(container,0x70)||at<void*>(container,0x28))return reject("container");
        void *rtv=nullptr,*dsv=nullptr;
        if(!vectorFirst(container,0x38,rtv,read)||!vectorFirst(container,0x50,dsv,read))return reject("view_vectors");
        auto* resource=at<ID3D11Resource*>(container,0x20);
        if(!read(resource,8))return reject("resource");
        ID3D11Texture2D* texture=nullptr;
        if(FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture))))return reject("texture_interface");
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        const bool cloned=desc.Usage==D3D11_USAGE_DEFAULT&&desc.CPUAccessFlags==0&&desc.MiscFlags==0&&
            e.clone.create(texture,static_cast<ID3D11DepthStencilView*>(dsv),
                at<ID3D11ShaderResourceView*>(container,0x30),static_cast<ID3D11RenderTargetView*>(rtv),
                at<ID3D11UnorderedAccessView*>(container,0x68),width_,height_);
        e.source=texture;texture->Release();
        if(!cloned){
            char message[320]{};std::snprintf(message,sizeof(message),
                "native_outer source role=%u size=%ux%u samples=%u quality=%u format=%u bind=%u usage=%u cpu=%u misc=%u mips=%u array=%u",
                index,desc.Width,desc.Height,desc.SampleDesc.Count,desc.SampleDesc.Quality,unsigned(desc.Format),
                desc.BindFlags,unsigned(desc.Usage),desc.CPUAccessFlags,desc.MiscFlags,desc.MipLevels,desc.ArraySize);
            log(message);return reject("texture_clone");
        }
        char name[128]{};std::snprintf(name,sizeof(name),"$LMUQV_Outer_%u_%ux%u_%llu_%llx",index,width_,height_,
            static_cast<unsigned long long>(generation_),static_cast<unsigned long long>(renderer_));
        using Factory=int(*)(void*,void**,const char*,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
        const int result=fn<Factory>(0x543950)(reinterpret_cast<void*>(renderer_+8),&e.wrapper,name,width_,height_,
            at<unsigned>(e.original,0x38),at<unsigned>(e.original,0x34),
            at<unsigned>(e.original,0),at<unsigned>(e.original,0x3c));
        if(result!=0||!read(e.wrapper,0xb0))return reject("native_factory");
        struct StringView {const char* data;std::size_t size;};
        StringView string{name,std::strlen(name)};
        void* allocation=fn<void*(*)(std::size_t)>(0x9ea868)(0x70);
        if(!allocation)return reject("native_allocation");
        void* replacement=fn<void*(*)(void*,const StringView*)>(0x603e90)(allocation,&string);
        if(replacement!=allocation)return reject("native_container");
        using Setter=void(*)(void*,void*);
        fn<Setter>(0x6056f0)(replacement,e.clone.texture());
        fn<Setter>(0x6053f0)(replacement,e.clone.dsv());
        fn<Setter>(0x605670)(replacement,e.clone.srv());
        fn<Setter>(0x6055c0)(replacement,e.clone.rtv());
        fn<Setter>(0x605770)(replacement,e.clone.uav());
        fn<void(*)(void*,void**)>(0x552d30)(e.wrapper,&replacement);
        if(replacement){fn<Setter>(0x5431b0)(nullptr,replacement);return reject("native_install");}
        void* installed=at<void*>(e.wrapper,0xa0);
        if(at<unsigned>(e.wrapper,0x28)!=width_||at<unsigned>(e.wrapper,0x2c)!=height_||
           !read(installed,0x70)||at<void*>(installed,0x20)!=e.clone.texture())return reject("installed_texture");
        char message[256]{};std::snprintf(message,sizeof(message),
            "native_outer created role=%u actual=%ux%u samples=%u format=%u original=%ux%u; internal PPFX pool unchanged",
            index,width_,height_,desc.SampleDesc.Count,unsigned(desc.Format),desc.Width,desc.Height);log(message);
        return true;
    }
    bool unchanged(const Entry& e,Readable read)const{
        if(!e.original)return true;
        if(!read(e.original,0xb0)||at<void*>(e.original,0xa8)!=e.associated)return false;
        void* container=at<void*>(e.original,0xa0);
        return read(container,0x70)&&at<void*>(container,0x20)==e.source;
    }
    bool associateDepths(Readable read,Log log){
        for(auto& e:entries_){
            if(!e.original)continue;
            Entry* depth=nullptr;
            if(e.associated){
                for(auto& candidate:entries_)if(candidate.original==e.associated)depth=&candidate;
                for(unsigned i=0;i<auxiliaryCount_;++i)if(auxiliary_[i].original==e.associated)depth=&auxiliary_[i];
                if(!depth){
                    if(auxiliaryCount_==auxiliary_.size()||!read(e.associated,0xb0)||at<void*>(e.associated,0xa8))return false;
                    depth=&auxiliary_[auxiliaryCount_++];depth->original=e.associated;
                    fn<void(*)(void*)>(0x54f680)(depth->original);
                    if(!cloneEntry(*depth,Count+auxiliaryCount_-1,read,log))return false;
                }
                if(!depth->clone.dsv())return false;
                D3D11_TEXTURE2D_DESC colorDesc{},depthDesc{};
                e.clone.texture()->GetDesc(&colorDesc);depth->clone.texture()->GetDesc(&depthDesc);
                if(colorDesc.Width!=depthDesc.Width||colorDesc.Height!=depthDesc.Height||
                   colorDesc.SampleDesc.Count!=depthDesc.SampleDesc.Count||colorDesc.SampleDesc.Quality!=depthDesc.SampleDesc.Quality)return false;
            }
            void* replacement=depth?depth->wrapper:nullptr;
            if(replacement)fn<void(*)(void*)>(0x54f680)(replacement);
            auto** slot=reinterpret_cast<void**>(static_cast<std::byte*>(e.wrapper)+0xa8);
            void* previous=*slot;*slot=replacement;
            if(previous)fn<void(*)(void*)>(0x552a00)(previous);
        }
        return true;
    }
public:
    NativeOuterBank()=default;
    NativeOuterBank(const NativeOuterBank&)=delete;
    NativeOuterBank& operator=(const NativeOuterBank&)=delete;
    // Explicit reset only while the native engine is alive and on its rendering
    // thread. A session destroyed on another thread retains this bounded bank
    // until process exit instead of calling native destructors concurrently.
    bool reset(){
        if(owner_&&owner_!=GetCurrentThreadId())return false;
        auto release=[&](Entry& e){
            if(e.wrapper)fn<void(*)(void*)>(0x552a00)(e.wrapper);
            if(e.original)fn<void(*)(void*)>(0x552a00)(e.original);
            e.wrapper=nullptr;e.original=nullptr;e.source=nullptr;e.associated=nullptr;e.slot=nullptr;e.clone.reset();
        };
        for(auto& e:entries_)release(e);
        for(auto& e:auxiliary_)release(e);
        auxiliaryCount_=0;
        ready_=false;width_=height_=0;renderer_=0;owner_=0;return true;
    }
    bool prepare(std::uintptr_t base,std::uintptr_t renderer,unsigned width,unsigned height,float clearDepth,Readable read,Log log){
        if(owner_&&owner_!=GetCurrentThreadId())return false;
        if(ready_){
            bool same=renderer==renderer_&&width==width_&&height==height_;
            for(auto& e:entries_)if(!read(e.slot,8)||*e.slot!=e.original||!unchanged(e,read))same=false;
            for(unsigned i=0;i<auxiliaryCount_;++i)if(!unchanged(auxiliary_[i],read))same=false;
            if(same)return true;
        }
        if(!reset())return false;
        base_=base;renderer_=renderer;width_=width;height_=height;owner_=GetCurrentThreadId();++generation_;
        const std::array<std::uintptr_t,Count> locations{base+0x1b1c750,base+0x1b1c758,base+0x1b1c740,base+0x1b1c748,
            renderer+0x82cc8,renderer+0x82cb0,renderer+0x82cc0,renderer+0x1948};
        for(unsigned i=0;i<Count;++i){
            auto& e=entries_[i];e.slot=reinterpret_cast<void**>(locations[i]);
            if(!read(e.slot,8)){reset();return false;}
            e.original=*e.slot;
            if(e.original){
                if(!read(e.original,0xa8)){e.original=nullptr;reset();return false;}
                fn<void(*)(void*)>(0x54f680)(e.original);
            } else if(i<=Eye1){reset();return false;}
            // Aliasing beyond the known independent banks needs a shared-owner
            // implementation; reject instead of silently splitting one target.
            for(unsigned j=0;j<i;++j)if(e.original&&e.original==entries_[j].original){
                char message[128]{};std::snprintf(message,sizeof(message),"native_outer rejected role=%u stage=alias otherRole=%u",i,j);
                log(message);reset();return false;
            }
            if(!cloneEntry(e,i,read,log)){reset();return false;}
        }
        if(!entries_[Hdr].clone.rtv()||!entries_[Depth].clone.dsv()||!entries_[Eye0].clone.rtv()||
           !entries_[Eye1].clone.rtv()){reset();return false;}
        D3D11_TEXTURE2D_DESC color{},depth{};
        entries_[Hdr].clone.texture()->GetDesc(&color);entries_[Depth].clone.texture()->GetDesc(&depth);
        if(color.SampleDesc.Count!=depth.SampleDesc.Count||color.SampleDesc.Quality!=depth.SampleDesc.Quality||
           !associateDepths(read,log)){reset();return false;}
        auto* context=*reinterpret_cast<ID3D11DeviceContext**>(renderer+0x76f68);
        if(!read(context,8)||(clearDepth!=0.f&&clearDepth!=1.f)){reset();return false;}
        const float black[4]{};
        auto clear=[&](Entry& e){
            if(e.clone.rtv())context->ClearRenderTargetView(e.clone.rtv(),black);
            if(e.clone.dsv()){
                D3D11_DEPTH_STENCIL_VIEW_DESC d{};e.clone.dsv()->GetDesc(&d);
                const unsigned flags=D3D11_CLEAR_DEPTH|((d.Format==DXGI_FORMAT_D24_UNORM_S8_UINT||
                    d.Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)?D3D11_CLEAR_STENCIL:0);
                context->ClearDepthStencilView(e.clone.dsv(),flags,clearDepth,0);
            }
        };
        for(auto& e:entries_)clear(e);
        for(unsigned i=0;i<auxiliaryCount_;++i)clear(auxiliary_[i]);
        ready_=true;return true;
    }
    bool bind(NativeOuterBindings& bindings){
        if(!ready_||owner_!=GetCurrentThreadId())return false;
        for(auto& e:entries_)if(!bindings.add(e.slot,e.wrapper))return false;
        for(unsigned eye=0;eye<2;++eye){
            auto& t=entries_[Eye0+eye].clone;
            if(!bindings.add(reinterpret_cast<ID3D11Texture2D**>(base_+0x1b1c6b8+eye*8),t.texture())||
               !bindings.add(reinterpret_cast<ID3D11RenderTargetView**>(base_+0x1b1c6c8+eye*8),t.rtv())||
               !bindings.add(reinterpret_cast<ID3D11ShaderResourceView**>(base_+0x1b1c6d8+eye*8),t.srv())||
               !bindings.add(reinterpret_cast<ID3D11UnorderedAccessView**>(base_+0x1b1c6e8+eye*8),t.uav()))return false;
        }
        auto& depth=entries_[Depth].clone;
        return bindings.add(reinterpret_cast<ID3D11Texture2D**>(base_+0x1b1c6f8),depth.texture())&&
            bindings.add(reinterpret_cast<ID3D11DepthStencilView**>(base_+0x1b1c700),depth.dsv())&&
            bindings.add(reinterpret_cast<ID3D11ShaderResourceView**>(base_+0x1b1c708),depth.srv());
    }
    bool mapTarget(void* original,void*& replacement)const{
        if(!original){replacement=nullptr;return true;}
        for(const auto& e:entries_)if(e.original==original){replacement=e.wrapper;return true;}
        return false;
    }
    ID3D11Texture2D* final(unsigned eye)const{return eye<2?entries_[Eye0+eye].clone.texture():nullptr;}
};
}
