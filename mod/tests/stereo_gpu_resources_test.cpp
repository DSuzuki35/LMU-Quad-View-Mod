#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../stereo_gpu_resources.hpp"
#include <cassert>
#include <iostream>
#include <thread>
using namespace lmuqv;
std::vector<uint32_t> buffer_bytes(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Resource* source){
    StereoComRef<ID3D11Buffer> buffer;assert(SUCCEEDED(source->QueryInterface(__uuidof(ID3D11Buffer),reinterpret_cast<void**>(buffer.put()))));
    D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    StereoComRef<ID3D11Buffer> staging;assert(SUCCEEDED(device->CreateBuffer(&desc,nullptr,staging.put())));context->CopyResource(staging.get(),source);
    D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(staging.get(),0,D3D11_MAP_READ,0,&map)));std::vector<uint32_t> bytes(desc.ByteWidth/4);std::memcpy(bytes.data(),map.pData,desc.ByteWidth);context->Unmap(staging.get(),0);return bytes;
}
std::vector<uint32_t> texture_bytes(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Resource* source){
    StereoComRef<ID3D11Texture2D> texture;assert(SUCCEEDED(source->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.put()))));
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.BindFlags=desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    StereoComRef<ID3D11Texture2D> staging;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,staging.put())));context->CopyResource(staging.get(),source);
    D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(staging.get(),0,D3D11_MAP_READ,0,&map)));std::vector<uint32_t> bytes(size_t(desc.Width)*desc.Height);
    for(unsigned row=0;row<desc.Height;++row)std::memcpy(bytes.data()+size_t(row)*desc.Width,static_cast<const uint8_t*>(map.pData)+size_t(row)*map.RowPitch,desc.Width*4);
    context->Unmap(staging.get(),0);return bytes;
}
int main(){
    StereoComRef<ID3D11Device> device;StereoComRef<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,device.put(),&level,context.put())));
    StereoFrameIdentity key{1,2,3,400,GetCurrentThreadId()};StereoGpuResourcePool pool;assert(pool.begin(context.get(),key));
    std::vector<uint32_t> initial(16),changed(16);for(unsigned i=0;i<16;++i){initial[i]=i+1;changed[i]=i+100;}
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=64;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;D3D11_SUBRESOURCE_DATA data{initial.data(),0,0};
    StereoComRef<ID3D11Buffer> source;assert(SUCCEEDED(device->CreateBuffer(&bd,&data,source.put())));
    auto first=pool.freeze(source.get());assert(first&&first.resource()!=source.get()&&pool.copied_bytes()==64);
    context->UpdateSubresource(source.get(),0,nullptr,changed.data(),0,0);auto second=pool.freeze(source.get());
    assert(second&&!first.same_contents(second)&&pool.copied_bytes()==128);
    assert(buffer_bytes(device.get(),context.get(),first.resource())==initial&&buffer_bytes(device.get(),context.get(),second.resource())==changed);
    bd.Usage=D3D11_USAGE_IMMUTABLE;StereoComRef<ID3D11Buffer> immutable;assert(SUCCEEDED(device->CreateBuffer(&bd,&data,immutable.put())));
    auto imm=pool.freeze(immutable.get()),immAgain=pool.freeze(immutable.get());assert(imm&&imm.same_contents(immAgain)&&imm.resource()==immutable.get());
    auto next=key;++next.frame;assert(pool.begin(context.get(),next));immAgain=pool.freeze(immutable.get());
    assert(imm.same_contents(immAgain)&&immAgain.identity()==next&&pool.copied_bytes()==0);
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=8;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    initial.assign(64,0x11223344);changed.assign(64,0x55667788);data={initial.data(),32,0};StereoComRef<ID3D11Texture2D> texture;
    assert(SUCCEEDED(device->CreateTexture2D(&td,&data,texture.put())));auto frozenTexture=pool.freeze(texture.get());assert(frozenTexture);
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=td.Format;sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;
    const auto view=pool.create_shader_view(frozenTexture,sv),sameView=pool.create_shader_view(frozenTexture,sv);assert(view&&view.same_binding(sameView));
    context->UpdateSubresource(texture.get(),0,nullptr,changed.data(),32,0);assert(texture_bytes(device.get(),context.get(),frozenTexture.resource())==initial);
    auto otherTexture=pool.freeze(texture.get());assert(otherTexture&&!frozenTexture.same_contents(otherTexture));
    assert(texture_bytes(device.get(),context.get(),otherTexture.resource())==changed);
    bool foreign=false,foreignBegin=true;std::thread worker([&]{foreign=bool(pool.freeze(texture.get()));foreignBegin=pool.begin(context.get(),next);});worker.join();assert(!foreign&&!foreignBegin);
    assert(pool.freeze(texture.get()));auto later=next;++later.frame;assert(pool.begin(context.get(),later));assert(!pool.create_shader_view(frozenTexture,sv));
    assert(pool.begin(context.get(),later,0));assert(!pool.freeze(texture.get())&&pool.copied_bytes()==0);assert(pool.freeze(immutable.get()));
    bd.Usage=D3D11_USAGE_DYNAMIC;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;StereoComRef<ID3D11Buffer> dynamic;
    assert(SUCCEEDED(device->CreateBuffer(&bd,nullptr,dynamic.put())));assert(!pool.freeze(dynamic.get()));
    uint64_t bytes=0;td.MipLevels=4;assert(stereo_gpu_detail::texture_bytes(td,bytes)&&bytes==340);
    td.Width=td.Height=5;td.MipLevels=1;td.Format=DXGI_FORMAT_BC7_UNORM;assert(stereo_gpu_detail::texture_bytes(td,bytes)&&bytes==64);
    assert(!pool.begin(context.get(),later,StereoGpuResourcePool::defaultByteBudget+1));
    std::cout<<"Stereo GPU resources WARP: frozen mutable contents survive later updates, immutable reuse, typed SRVs, frame/thread guards, byte budgets and unmapped-producer refusal passed\n";
}
