#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "native_sps.hpp"
#include "native_sps_runtime.hpp"
#include "caller_address.hpp"
#include "vendor/minhook-1.3.4/include/MinHook.h"
#include <d3d11_4.h>

namespace lmuqv { namespace native_sps_hooks {
enum Method:unsigned {Indexed,Plain,IndexedInstanced,Instanced,Automatic,IndexedIndirect,Indirect,Dispatch,DispatchIndirect,
    Map,Unmap,Update,Copy,CopyRegion,Resolve,ClearRt,ClearDepth,ClearUint,ClearFloat,Mips,Execute,Begin,End,
    SetRt,SetRtUav,SetCsUav,ClearState,CreateVs,CreatePs,CreateLayout,CopyCount,SetSo,
    CopyRegion1,Update1,DiscardResource,DiscardView,SwapState,ClearView,DiscardView1,WriteSubresource,Count};
constexpr std::array<unsigned,Count> indices{12,13,20,21,38,39,40,41,42,14,15,48,47,46,57,50,53,51,52,54,58,27,28,33,34,68,110,12,15,11,49,37,115,116,117,118,131,132,133,63};
struct Hook {void* target=nullptr;void* original=nullptr;bool created=false;};
std::array<Hook,Count> methods;
std::array<Hook,2> factories;
std::atomic<unsigned> active{0};
std::atomic<bool> armed{false};
thread_local bool inFactory=false;
void(*logger)(const char*)=nullptr;
struct Call {Call(){active.fetch_add(1,std::memory_order_acq_rel);}~Call(){active.fetch_sub(1,std::memory_order_acq_rel);}};
struct ContextCall:Call {
    std::unique_lock<std::recursive_mutex> lock;
    bool safe=true;
    explicit ContextCall(ID3D11DeviceContext* context){
        try{lock=nativeSpsRuntime.api_lock(context);}catch(...){safe=false;nativeSpsRuntime.unknown_commands(context);}
    }
};
template<class Fn,Method M>Fn original(){return reinterpret_cast<Fn>(methods[M].original);}
void view_written(ID3D11DeviceContext* context,ID3D11View* view){
    if(native_sps_detail::internalDepth||!view||!nativeSpsRuntime.context(context))return;
    StereoComRef<ID3D11Resource> resource;view->GetResource(resource.put());nativeSpsRuntime.written(context,resource.get());
}
void STDMETHODCALLTYPE indexed(ID3D11DeviceContext* c,UINT n,UINT start,INT base){
    ContextCall scope(c);if(scope.safe&&nativeSpsRuntime.indexed(c,n,1,start,base,0))return;
    if(scope.safe)nativeSpsRuntime.gpu_writes(c,false);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);original<Fn,Indexed>()(c,n,start,base);
}
void STDMETHODCALLTYPE indexed_instanced(ID3D11DeviceContext* c,UINT n,UINT instances,UINT start,INT base,UINT first){
    ContextCall scope(c);if(scope.safe&&nativeSpsRuntime.indexed(c,n,instances,start,base,first))return;
    if(scope.safe)nativeSpsRuntime.gpu_writes(c,false);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);original<Fn,IndexedInstanced>()(c,n,instances,start,base,first);
}
void STDMETHODCALLTYPE plain(ID3D11DeviceContext* c,UINT n,UINT first){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.gpu_writes(c,false);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);original<Fn,Plain>()(c,n,first);}
void STDMETHODCALLTYPE instanced(ID3D11DeviceContext* c,UINT n,UINT instances,UINT first,UINT firstInstance){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.gpu_writes(c,false);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);original<Fn,Instanced>()(c,n,instances,first,firstInstance);}
void STDMETHODCALLTYPE automatic(ID3D11DeviceContext* c){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.gpu_writes(c,false);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);original<Fn,Automatic>()(c);}
template<Method M>void STDMETHODCALLTYPE indirect(ID3D11DeviceContext* c,ID3D11Buffer* args,UINT offset){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.gpu_writes(c,M==DispatchIndirect);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);original<Fn,M>()(c,args,offset);}
void STDMETHODCALLTYPE dispatch(ID3D11DeviceContext* c,UINT x,UINT y,UINT z){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.gpu_writes(c,true);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT);original<Fn,Dispatch>()(c,x,y,z);}
HRESULT STDMETHODCALLTYPE map(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE* result){
    ContextCall scope(c);using Fn=HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
    const auto hr=original<Fn,Map>()(c,r,sub,type,flags,result);if(scope.safe&&SUCCEEDED(hr)&&type!=D3D11_MAP_READ)nativeSpsRuntime.written(c,r);return hr;
}
void STDMETHODCALLTYPE unmap(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.written(c,r);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT);original<Fn,Unmap>()(c,r,sub);}
void STDMETHODCALLTYPE update(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub,const D3D11_BOX* box,const void* data,UINT row,UINT depth){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.written(c,r);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);original<Fn,Update>()(c,r,sub,box,data,row,depth);}
void STDMETHODCALLTYPE copy(ID3D11DeviceContext* c,ID3D11Resource* dst,ID3D11Resource* src){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.written(c,dst);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*);original<Fn,Copy>()(c,dst,src);}
void STDMETHODCALLTYPE copy_region(ID3D11DeviceContext* c,ID3D11Resource* dst,UINT sub,UINT x,UINT y,UINT z,ID3D11Resource* src,UINT srcSub,const D3D11_BOX* box){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.written(c,dst);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);original<Fn,CopyRegion>()(c,dst,sub,x,y,z,src,srcSub,box);}
void STDMETHODCALLTYPE resolve(ID3D11DeviceContext* c,ID3D11Resource* dst,UINT sub,ID3D11Resource* src,UINT srcSub,DXGI_FORMAT format){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.written(c,dst);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,ID3D11Resource*,UINT,DXGI_FORMAT);original<Fn,Resolve>()(c,dst,sub,src,srcSub,format);}
void STDMETHODCALLTYPE clear_rt(ID3D11DeviceContext* c,ID3D11RenderTargetView* view,const FLOAT color[4]){ContextCall scope(c);if(scope.safe)view_written(c,view);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11RenderTargetView*,const FLOAT*);original<Fn,ClearRt>()(c,view,color);}
void STDMETHODCALLTYPE clear_depth(ID3D11DeviceContext* c,ID3D11DepthStencilView* view,UINT flags,FLOAT depth,UINT8 stencil){ContextCall scope(c);if(scope.safe)view_written(c,view);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11DepthStencilView*,UINT,FLOAT,UINT8);original<Fn,ClearDepth>()(c,view,flags,depth,stencil);}
void STDMETHODCALLTYPE clear_uint(ID3D11DeviceContext* c,ID3D11UnorderedAccessView* view,const UINT values[4]){ContextCall scope(c);if(scope.safe)view_written(c,view);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11UnorderedAccessView*,const UINT*);original<Fn,ClearUint>()(c,view,values);}
void STDMETHODCALLTYPE clear_float(ID3D11DeviceContext* c,ID3D11UnorderedAccessView* view,const FLOAT values[4]){ContextCall scope(c);if(scope.safe)view_written(c,view);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11UnorderedAccessView*,const FLOAT*);original<Fn,ClearFloat>()(c,view,values);}
void STDMETHODCALLTYPE mips(ID3D11DeviceContext* c,ID3D11ShaderResourceView* view){ContextCall scope(c);if(scope.safe)view_written(c,view);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11ShaderResourceView*);original<Fn,Mips>()(c,view);}
void STDMETHODCALLTYPE execute(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore){
    ContextCall scope(c);if(scope.safe)nativeSpsRuntime.command_list(c);
    using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);
    {native_sps_detail::Internal replay;original<Fn,Execute>()(c,list,restore);}
    nativeSpsRuntime.outputs_changed(c);
}
void STDMETHODCALLTYPE begin(ID3D11DeviceContext* c,ID3D11Asynchronous* q){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.begin_query(c,q);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Asynchronous*);original<Fn,Begin>()(c,q);}
void STDMETHODCALLTYPE end(ID3D11DeviceContext* c,ID3D11Asynchronous* q){ContextCall scope(c);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Asynchronous*);original<Fn,End>()(c,q);if(scope.safe)nativeSpsRuntime.end_query(c,q);}
void STDMETHODCALLTYPE set_rt(ID3D11DeviceContext* c,UINT count,ID3D11RenderTargetView*const* rt,ID3D11DepthStencilView* depth){ContextCall scope(c);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView*const*,ID3D11DepthStencilView*);original<Fn,SetRt>()(c,count,rt,depth);nativeSpsRuntime.outputs_changed(c);}
void STDMETHODCALLTYPE set_rt_uav(ID3D11DeviceContext* c,UINT count,ID3D11RenderTargetView*const* rt,ID3D11DepthStencilView* depth,UINT start,UINT uavCount,ID3D11UnorderedAccessView*const* uav,const UINT* counters){ContextCall scope(c);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView*const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView*const*,const UINT*);original<Fn,SetRtUav>()(c,count,rt,depth,start,uavCount,uav,counters);nativeSpsRuntime.outputs_changed(c);}
void STDMETHODCALLTYPE set_cs_uav(ID3D11DeviceContext* c,UINT start,UINT count,ID3D11UnorderedAccessView*const* uav,const UINT* counters){ContextCall scope(c);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11UnorderedAccessView*const*,const UINT*);original<Fn,SetCsUav>()(c,start,count,uav,counters);nativeSpsRuntime.outputs_changed(c);}
void STDMETHODCALLTYPE clear_state(ID3D11DeviceContext* c){ContextCall scope(c);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);original<Fn,ClearState>()(c);nativeSpsRuntime.outputs_changed(c);}
template<class T,Method M>HRESULT STDMETHODCALLTYPE create_shader(ID3D11Device* d,const void* code,SIZE_T bytes,ID3D11ClassLinkage* linkage,T** result){
    Call scope;using Fn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,T**);
    const auto hr=original<Fn,M>()(d,code,bytes,linkage,result);
    if(SUCCEEDED(hr)&&result&&*result)try{nativeSpsRuntime.observe_shader(d,*result,code,bytes,M==CreateVs);}catch(...){}
    return hr;
}
HRESULT STDMETHODCALLTYPE create_layout(ID3D11Device* d,const D3D11_INPUT_ELEMENT_DESC* elements,UINT count,const void* code,SIZE_T bytes,ID3D11InputLayout** result){
    Call scope;using Fn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_INPUT_ELEMENT_DESC*,UINT,const void*,SIZE_T,ID3D11InputLayout**);
    const auto hr=original<Fn,CreateLayout>()(d,elements,count,code,bytes,result);
    if(SUCCEEDED(hr)&&result&&*result)try{nativeSpsRuntime.observe_layout(d,*result,elements,count);}catch(...){}
    return hr;
}
void STDMETHODCALLTYPE copy_count(ID3D11DeviceContext* c,ID3D11Buffer* dst,UINT offset,ID3D11UnorderedAccessView* source){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.written(c,dst);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT,ID3D11UnorderedAccessView*);original<Fn,CopyCount>()(c,dst,offset,source);}
void STDMETHODCALLTYPE set_so(ID3D11DeviceContext* c,UINT count,ID3D11Buffer*const* buffers,const UINT* offsets){ContextCall scope(c);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11Buffer*const*,const UINT*);original<Fn,SetSo>()(c,count,buffers,offsets);nativeSpsRuntime.outputs_changed(c);}
void STDMETHODCALLTYPE copy_region1(ID3D11DeviceContext1* c,ID3D11Resource* dst,UINT sub,UINT x,UINT y,UINT z,ID3D11Resource* src,UINT srcSub,const D3D11_BOX* box,UINT flags){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.written(c,dst);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*,UINT);original<Fn,CopyRegion1>()(c,dst,sub,x,y,z,src,srcSub,box,flags);}
void STDMETHODCALLTYPE update1(ID3D11DeviceContext1* c,ID3D11Resource* dst,UINT sub,const D3D11_BOX* box,const void* data,UINT row,UINT depth,UINT flags){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.written(c,dst);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT,UINT);original<Fn,Update1>()(c,dst,sub,box,data,row,depth,flags);}
void STDMETHODCALLTYPE discard_resource(ID3D11DeviceContext1* c,ID3D11Resource* resource){ContextCall scope(c);if(scope.safe)nativeSpsRuntime.written(c,resource);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11Resource*);original<Fn,DiscardResource>()(c,resource);}
void STDMETHODCALLTYPE discard_view(ID3D11DeviceContext1* c,ID3D11View* view){ContextCall scope(c);if(scope.safe)view_written(c,view);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11View*);original<Fn,DiscardView>()(c,view);}
void STDMETHODCALLTYPE swap_state(ID3D11DeviceContext1* c,ID3DDeviceContextState* state,ID3DDeviceContextState** previous){ContextCall scope(c);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3DDeviceContextState*,ID3DDeviceContextState**);original<Fn,SwapState>()(c,state,previous);nativeSpsRuntime.outputs_changed(c);}
void STDMETHODCALLTYPE clear_view(ID3D11DeviceContext1* c,ID3D11View* view,const FLOAT color[4],const D3D11_RECT* rects,UINT count){ContextCall scope(c);if(scope.safe)view_written(c,view);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11View*,const FLOAT*,const D3D11_RECT*,UINT);original<Fn,ClearView>()(c,view,color,rects,count);}
void STDMETHODCALLTYPE discard_view1(ID3D11DeviceContext1* c,ID3D11View* view,const D3D11_RECT* rects,UINT count){ContextCall scope(c);if(scope.safe)view_written(c,view);using Fn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11View*,const D3D11_RECT*,UINT);original<Fn,DiscardView1>()(c,view,rects,count);}
void STDMETHODCALLTYPE write_subresource(ID3D11Device3* d,ID3D11Resource* dst,UINT sub,const D3D11_BOX* box,const void* data,UINT row,UINT depth){
    auto* context=nativeSpsRuntime.selected_context();ContextCall call(context);
    StereoComRef<ID3D11Device> actual;if(dst)dst->GetDevice(actual.put());if(call.safe&&nativeSpsRuntime.device(actual.get()))nativeSpsRuntime.written(context,dst);
    using Fn=void(STDMETHODCALLTYPE*)(ID3D11Device3*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);original<Fn,WriteSubresource>()(d,dst,sub,box,data,row,depth);
}
const std::array<void*,Count> detours{
    reinterpret_cast<void*>(&indexed),reinterpret_cast<void*>(&plain),reinterpret_cast<void*>(&indexed_instanced),reinterpret_cast<void*>(&instanced),reinterpret_cast<void*>(&automatic),
    reinterpret_cast<void*>(&indirect<IndexedIndirect>),reinterpret_cast<void*>(&indirect<Indirect>),reinterpret_cast<void*>(&dispatch),reinterpret_cast<void*>(&indirect<DispatchIndirect>),
    reinterpret_cast<void*>(&map),reinterpret_cast<void*>(&unmap),reinterpret_cast<void*>(&update),reinterpret_cast<void*>(&copy),reinterpret_cast<void*>(&copy_region),reinterpret_cast<void*>(&resolve),
    reinterpret_cast<void*>(&clear_rt),reinterpret_cast<void*>(&clear_depth),reinterpret_cast<void*>(&clear_uint),reinterpret_cast<void*>(&clear_float),reinterpret_cast<void*>(&mips),
    reinterpret_cast<void*>(&execute),reinterpret_cast<void*>(&begin),reinterpret_cast<void*>(&end),reinterpret_cast<void*>(&set_rt),reinterpret_cast<void*>(&set_rt_uav),reinterpret_cast<void*>(&set_cs_uav),
    reinterpret_cast<void*>(&clear_state),reinterpret_cast<void*>(&create_shader<ID3D11VertexShader,CreateVs>),reinterpret_cast<void*>(&create_shader<ID3D11PixelShader,CreatePs>),reinterpret_cast<void*>(&create_layout),
    reinterpret_cast<void*>(&copy_count),reinterpret_cast<void*>(&set_so),reinterpret_cast<void*>(&copy_region1),reinterpret_cast<void*>(&update1),reinterpret_cast<void*>(&discard_resource),
    reinterpret_cast<void*>(&discard_view),reinterpret_cast<void*>(&swap_state),reinterpret_cast<void*>(&clear_view),reinterpret_cast<void*>(&discard_view1),reinterpret_cast<void*>(&write_subresource)};
void remove_methods(){for(auto& hook:methods)if(hook.created)MH_DisableHook(hook.target);for(auto& hook:methods)if(hook.created){MH_RemoveHook(hook.target);hook.created=false;}}
bool device_aliases(ID3D11Device* d,ID3D11DeviceContext* c){
    auto** base=*reinterpret_cast<void***>(c);
    const auto check=[&](ID3D11DeviceContext* alias){auto** table=*reinterpret_cast<void***>(alias);
        for(unsigned i=0;i<32;++i)if(i<27||i>29)if(table[indices[i]]!=base[indices[i]])return false;
        nativeSpsRuntime.add_context_alias(alias);return true;};
    StereoComRef<ID3D11DeviceContext1> c1;StereoComRef<ID3D11DeviceContext2> c2;StereoComRef<ID3D11DeviceContext3> c3;StereoComRef<ID3D11DeviceContext4> c4;
    if(SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(c1.put())))){
        if(!check(c1.get()))return false;auto** table=*reinterpret_cast<void***>(c1.get());for(unsigned i=32;i<39;++i)methods[i].target=table[indices[i]];}
    if(SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext2),reinterpret_cast<void**>(c2.put())))&&!check(c2.get()))return false;
    if(SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext3),reinterpret_cast<void**>(c3.put())))&&!check(c3.get()))return false;
    if(SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext4),reinterpret_cast<void**>(c4.put())))&&!check(c4.get()))return false;
    if(c1){const auto check1=[&](ID3D11DeviceContext1* alias){auto** table=*reinterpret_cast<void***>(alias);for(unsigned i=32;i<39;++i)if(table[indices[i]]!=methods[i].target)return false;return true;};
        if((c2&&!check1(c2.get()))||(c3&&!check1(c3.get()))||(c4&&!check1(c4.get())))return false;}
    auto** deviceTable=*reinterpret_cast<void***>(d);
    const auto checkDevice=[&](ID3D11Device* alias){auto** table=*reinterpret_cast<void***>(alias);for(unsigned i=27;i<30;++i)if(table[indices[i]]!=deviceTable[indices[i]])return false;nativeSpsRuntime.add_device_alias(alias);return true;};
    StereoComRef<ID3D11Device1> d1;StereoComRef<ID3D11Device2> d2;StereoComRef<ID3D11Device3> d3;StereoComRef<ID3D11Device4> d4;StereoComRef<ID3D11Device5> d5;
    if(SUCCEEDED(d->QueryInterface(__uuidof(ID3D11Device1),reinterpret_cast<void**>(d1.put())))&&!checkDevice(d1.get()))return false;
    if(SUCCEEDED(d->QueryInterface(__uuidof(ID3D11Device2),reinterpret_cast<void**>(d2.put())))&&!checkDevice(d2.get()))return false;
    if(SUCCEEDED(d->QueryInterface(__uuidof(ID3D11Device3),reinterpret_cast<void**>(d3.put())))){if(!checkDevice(d3.get()))return false;methods[WriteSubresource].target=(*reinterpret_cast<void***>(d3.get()))[indices[WriteSubresource]];}
    if(SUCCEEDED(d->QueryInterface(__uuidof(ID3D11Device4),reinterpret_cast<void**>(d4.put())))&&!checkDevice(d4.get()))return false;
    if(SUCCEEDED(d->QueryInterface(__uuidof(ID3D11Device5),reinterpret_cast<void**>(d5.put())))&&!checkDevice(d5.get()))return false;
    if(d3&&((d4&&(*reinterpret_cast<void***>(d4.get()))[63]!=methods[WriteSubresource].target)||(d5&&(*reinterpret_cast<void***>(d5.get()))[63]!=methods[WriteSubresource].target)))return false;
    return true;
}
bool main_caller(const void* caller){HMODULE module=nullptr;return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(caller),&module)&&module==GetModuleHandleW(nullptr);}
struct FactoryCall:Call {bool previous=inFactory;FactoryCall(){inFactory=true;}~FactoryCall(){inFactory=previous;}};
void attach_device(ID3D11Device* device,ID3D11DeviceContext* context){
    if(!armed||nativeSpsRuntime.enabled()||!device)return;
    StereoComRef<ID3D11DeviceContext> immediate;if(context)immediate=StereoComRef<ID3D11DeviceContext>(context);else device->GetImmediateContext(immediate.put());
    native_sps_attach_owned_device(immediate.get(),reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),logger);
}
HRESULT WINAPI create_device(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context){
    const bool observe=!inFactory&&main_caller(LMU_CALLER_ADDRESS);FactoryCall scope;
    auto fn=reinterpret_cast<decltype(&D3D11CreateDevice)>(factories[0].original);const auto hr=fn(adapter,type,software,flags,levels,count,sdk,device,level,context);
    if(observe&&SUCCEEDED(hr)&&device&&*device)try{attach_device(*device,context?*context:nullptr);}catch(...){}return hr;
}
HRESULT WINAPI create_device_swapchain(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,const DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** swapchain,ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context){
    const bool observe=!inFactory&&main_caller(LMU_CALLER_ADDRESS);FactoryCall scope;
    auto fn=reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(factories[1].original);const auto hr=fn(adapter,type,software,flags,levels,count,sdk,desc,swapchain,device,level,context);
    if(observe&&SUCCEEDED(hr)&&device&&*device)try{attach_device(*device,context?*context:nullptr);}catch(...){}return hr;
}
} // namespace native_sps_hooks
bool native_sps_attach_owned_device(ID3D11DeviceContext* context,uintptr_t image,void(*log)(const char*)){
    using namespace native_sps_hooks;
    if(nativeSpsRuntime.enabled()||!context||!image)return false;logger=log;
    if(std::any_of(methods.begin(),methods.end(),[](const Hook& h){return h.created;}))return false;
    methods={};
    const auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(!nativeSpsRuntime.configure_early(context,image,log))return false;
    const auto fail=[&](const char* reason){if(log)log(reason);remove_methods();nativeSpsRuntime.stop();return false;};
    StereoComRef<ID3D11Device> device;context->GetDevice(device.put());
    auto** table=*reinterpret_cast<void***>(context);auto** deviceTable=*reinterpret_cast<void***>(device.get());
    for(unsigned i=0;i<32;++i)methods[i].target=(i>=27&&i<=29?deviceTable:table)[indices[i]];
    if(!device_aliases(device.get(),context))return fail("sps_render unavailable: D3D interface method identities differ");
    for(unsigned i=0;i<Count;++i)if(methods[i].target){
        for(unsigned j=0;j<i;++j)if(methods[j].target==methods[i].target)return fail("sps_render unavailable: aliased D3D mutation methods");
        const auto result=MH_CreateHook(methods[i].target,detours[i],&methods[i].original);
        if(result!=MH_OK){if(log){char line[160]{};std::snprintf(line,sizeof(line),"sps_render D3D hook %u creation failed status=%d",i,int(result));log(line);}return fail("sps_render unavailable: complete D3D hook coverage required");}
        methods[i].created=true;
    }
    for(auto& hook:methods)if(hook.created&&MH_EnableHook(hook.target)!=MH_OK)return fail("sps_render unavailable: D3D hook activation failed");
    if(log)log("sps_render early device hooks ready: shaders/layouts, query boundaries and resource mutations; native drawing awaits engine scopes");return true;
}
bool native_sps_prepare(bool enable,void(*log)(const char*)){
    using namespace native_sps_hooks;
    if(!enable||armed)return false;logger=log;
    const auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return false;
    auto module=GetModuleHandleW(L"d3d11.dll");if(!module)module=LoadLibraryExW(L"d3d11.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)return false;
    const std::array<const char*,2> names{"D3D11CreateDevice","D3D11CreateDeviceAndSwapChain"};
    const std::array<void*,2> callbacks{reinterpret_cast<void*>(&create_device),reinterpret_cast<void*>(&create_device_swapchain)};
    for(unsigned i=0;i<2;++i){factories[i].target=reinterpret_cast<void*>(GetProcAddress(module,names[i]));
        if(!factories[i].target||MH_CreateHook(factories[i].target,callbacks[i],&factories[i].original)!=MH_OK){native_sps_stop();return false;}factories[i].created=true;}
    armed=true;for(auto& hook:factories)if(MH_EnableHook(hook.target)!=MH_OK){native_sps_stop();return false;}
    if(log)log("sps_render early D3D factory hooks armed");return true;
}
bool native_sps_device_matches(ID3D11DeviceContext* context){return nativeSpsRuntime.context(context);}
void native_sps_stop(){
    using namespace native_sps_hooks;armed=false;
    for(auto& hook:factories)if(hook.created)MH_DisableHook(hook.target);
    for(auto& hook:methods)if(hook.created)MH_DisableHook(hook.target);
    for(unsigned i=0;active.load(std::memory_order_acquire)&&i<5000;++i)Sleep(1);
    if(active.load(std::memory_order_acquire)){if(logger)logger("sps_render teardown deferred: D3D call still active");return;}
    remove_methods();for(auto& hook:factories)if(hook.created){MH_RemoveHook(hook.target);hook.created=false;}
    nativeSpsRuntime.stop();logger=nullptr;
}
}
