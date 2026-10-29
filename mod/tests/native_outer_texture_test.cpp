// Offline real-D3D11 WARP tests. No game or hardware-device access.
#include "../native_outer_texture.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
template<class T>struct Com {
    T* p=nullptr;
    ~Com(){if(p)p->Release();}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    T** put(){return &p;}
    T* operator->()const{return p;}
};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void check(HRESULT hr,const char* message){require(SUCCEEDED(hr),message);}
struct Device {
    Com<ID3D11Device> d;Com<ID3D11DeviceContext> c;
    Device(){check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,d.put(),nullptr,c.put()),"WARP create");}
    ~Device(){c->ClearState();}
};
template<class V>void belongs(V* view,ID3D11Texture2D* texture){
    Com<ID3D11Resource> resource;view->GetResource(resource.put());
    require(resource.p==static_cast<ID3D11Resource*>(texture),"cloned view resource mismatch");
}
template<class V,class Desc>void equal_view(V* first,V* second){
    Desc a{},b{};first->GetDesc(&a);second->GetDesc(&b);
    require(std::memcmp(&a,&b,sizeof(a))==0,"cloned view descriptor changed");
}
void same_texture_desc(ID3D11Texture2D* source,ID3D11Texture2D* result,UINT width,UINT height){
    D3D11_TEXTURE2D_DESC a{},b{};source->GetDesc(&a);result->GetDesc(&b);
    a.Width=width;a.Height=height;
    require(std::memcmp(&a,&b,sizeof(a))==0,"texture descriptor changed beyond dimensions");
}
void empty(const lmuqv::NativeOuterTexture& clone){
    require(!clone.texture()&&!clone.dsv()&&!clone.srv()&&!clone.rtv()&&!clone.uav(),"expected empty clone");
}
void color(Device& device,DXGI_FORMAT format){
    D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=48;desc.MipLevels=desc.ArraySize=1;
    desc.Format=format;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    Com<ID3D11Texture2D> original;check(device.d->CreateTexture2D(&desc,nullptr,original.put()),"color texture");
    Com<ID3D11RenderTargetView> rtv;Com<ID3D11ShaderResourceView> srv;Com<ID3D11UnorderedAccessView> uav;
    D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ud.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
    check(device.d->CreateRenderTargetView(original.p,&rd,rtv.put()),"color RTV");
    check(device.d->CreateShaderResourceView(original.p,&sd,srv.put()),"color SRV");
    check(device.d->CreateUnorderedAccessView(original.p,&ud,uav.put()),"color UAV");
    lmuqv::NativeOuterTexture clone;empty(clone);
    require(clone.create(original.p,nullptr,srv.p,rtv.p,uav.p,16,12),"color clone create");
    require(clone.texture()!=original.p&&!clone.dsv(),"clone did not create independent color resource");
    same_texture_desc(original.p,clone.texture(),16,12);
    belongs(clone.rtv(),clone.texture());belongs(clone.srv(),clone.texture());belongs(clone.uav(),clone.texture());
    equal_view<ID3D11RenderTargetView,D3D11_RENDER_TARGET_VIEW_DESC>(rtv.p,clone.rtv());
    equal_view<ID3D11ShaderResourceView,D3D11_SHADER_RESOURCE_VIEW_DESC>(srv.p,clone.srv());
    equal_view<ID3D11UnorderedAccessView,D3D11_UNORDERED_ACCESS_VIEW_DESC>(uav.p,clone.uav());
    const float green[4]{0,1,0,1};device.c->ClearRenderTargetView(clone.rtv(),green);
    D3D11_TEXTURE2D_DESC read{};clone.texture()->GetDesc(&read);read.BindFlags=0;
    read.Usage=D3D11_USAGE_STAGING;read.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Com<ID3D11Texture2D> staging;check(device.d->CreateTexture2D(&read,nullptr,staging.put()),"staging");
    device.c->CopyResource(staging.p,clone.texture());D3D11_MAPPED_SUBRESOURCE map{};
    check(device.c->Map(staging.p,0,D3D11_MAP_READ,0,&map),"map clone");
    auto* first=static_cast<const unsigned char*>(map.pData);
    const bool greenPixel=first[0]==0&&first[1]==255&&first[2]==0&&first[3]==255;
    device.c->Unmap(staging.p,0);require(greenPixel,"cloned RTV not writable");

    auto* saved=clone.texture();auto* savedRtv=clone.rtv();
    for(const auto& size:{std::pair<UINT,UINT>{0,12},{16,0},{65,12},{16,49},{16385,12}}){
        require(!clone.create(original.p,nullptr,srv.p,rtv.p,uav.p,size.first,size.second),"invalid size accepted");
        require(clone.texture()==saved&&clone.rtv()==savedRtv,"invalid size changed old state");
    }
    require(!clone.create(nullptr,nullptr,nullptr,nullptr,nullptr,16,12)&&clone.texture()==saved,"null source changed state");
    Com<ID3D11Texture2D> other;check(device.d->CreateTexture2D(&desc,nullptr,other.put()),"other texture");
    Com<ID3D11RenderTargetView> otherRtv;check(device.d->CreateRenderTargetView(other.p,&rd,otherRtv.put()),"other RTV");
    require(!clone.create(original.p,nullptr,srv.p,otherRtv.p,uav.p,16,12)&&clone.texture()==saved,"foreign resource accepted");
    Device second;Com<ID3D11Texture2D> otherDeviceTexture;
    check(second.d->CreateTexture2D(&desc,nullptr,otherDeviceTexture.put()),"other device texture");
    Com<ID3D11ShaderResourceView> otherSrv;check(second.d->CreateShaderResourceView(otherDeviceTexture.p,&sd,otherSrv.put()),"other device SRV");
    require(!clone.create(original.p,nullptr,otherSrv.p,rtv.p,uav.p,16,12)&&clone.texture()==saved,"foreign device accepted");
    // Immutable descriptor is supported by original+data, but cannot be cloned
    // without its contents. Rejecting it must preserve the previous clone.
    desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    unsigned char pixels[64*48*4]{};D3D11_SUBRESOURCE_DATA initial{pixels,64*4,0};
    Com<ID3D11Texture2D> immutable;check(device.d->CreateTexture2D(&desc,&initial,immutable.put()),"immutable original");
    require(!clone.create(immutable.p,nullptr,nullptr,nullptr,nullptr,16,12)&&clone.texture()==saved,"failed allocation changed state");
    require(clone.create(original.p,nullptr,nullptr,nullptr,nullptr,64,48),"same-size texture-only create");
    require(clone.texture()!=saved&&!clone.rtv()&&!clone.srv()&&!clone.uav(),"replacement retained old views");
    clone.reset();clone.reset();empty(clone);
    desc.Usage=D3D11_USAGE_DEFAULT;desc.ArraySize=2;
    Com<ID3D11Texture2D> array;check(device.d->CreateTexture2D(&desc,nullptr,array.put()),"array original");
    require(!clone.create(array.p,nullptr,nullptr,nullptr,nullptr,16,12),"array accepted");empty(clone);
    desc.ArraySize=1;desc.MipLevels=2;
    Com<ID3D11Texture2D> mips;check(device.d->CreateTexture2D(&desc,nullptr,mips.put()),"mips original");
    require(!clone.create(mips.p,nullptr,nullptr,nullptr,nullptr,16,12),"mips accepted");empty(clone);
    std::cout<<"color format="<<unsigned(format)<<" descriptors/views/readback/atomic failure/reset passed\n";
}
void depth(Device& device){
    D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=48;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R24G8_TYPELESS;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture2D> original;check(device.d->CreateTexture2D(&desc,nullptr,original.put()),"depth texture");
    D3D11_DEPTH_STENCIL_VIEW_DESC dd{};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    Com<ID3D11DepthStencilView> dsv;check(device.d->CreateDepthStencilView(original.p,&dd,dsv.put()),"depth DSV");
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
    Com<ID3D11ShaderResourceView> srv;check(device.d->CreateShaderResourceView(original.p,&sd,srv.put()),"depth SRV");
    lmuqv::NativeOuterTexture clone;
    require(clone.create(original.p,dsv.p,srv.p,nullptr,nullptr,16,12),"typeless depth clone");
    same_texture_desc(original.p,clone.texture(),16,12);belongs(clone.dsv(),clone.texture());belongs(clone.srv(),clone.texture());
    equal_view<ID3D11DepthStencilView,D3D11_DEPTH_STENCIL_VIEW_DESC>(dsv.p,clone.dsv());
    equal_view<ID3D11ShaderResourceView,D3D11_SHADER_RESOURCE_VIEW_DESC>(srv.p,clone.srv());
    device.c->ClearDepthStencilView(clone.dsv(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,.5f,37);
    clone.reset();clone.reset();empty(clone);
    std::cout<<"typeless depth DSV/SRV passed\n";
}
void msaa(Device& device){
    UINT quality=0;check(device.d->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,2,&quality),"MSAA query");
    if(!quality){std::cout<<"SKIP WARP MSAA2 unsupported\n";return;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=64;desc.Height=48;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=2;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture2D> original;check(device.d->CreateTexture2D(&desc,nullptr,original.put()),"MSAA texture");
    Com<ID3D11RenderTargetView> rtv;Com<ID3D11ShaderResourceView> srv;
    check(device.d->CreateRenderTargetView(original.p,nullptr,rtv.put()),"MSAA RTV");
    check(device.d->CreateShaderResourceView(original.p,nullptr,srv.put()),"MSAA SRV");
    lmuqv::NativeOuterTexture clone;require(clone.create(original.p,nullptr,srv.p,rtv.p,nullptr,16,12),"MSAA clone");
    same_texture_desc(original.p,clone.texture(),16,12);belongs(clone.rtv(),clone.texture());belongs(clone.srv(),clone.texture());
    equal_view<ID3D11RenderTargetView,D3D11_RENDER_TARGET_VIEW_DESC>(rtv.p,clone.rtv());
    equal_view<ID3D11ShaderResourceView,D3D11_SHADER_RESOURCE_VIEW_DESC>(srv.p,clone.srv());
    const float blue[4]{0,0,1,1};device.c->ClearRenderTargetView(clone.rtv(),blue);
    std::cout<<"WARP MSAA2 RTV/SRV passed\n";
}
}
int main(){try{Device device;color(device,DXGI_FORMAT_R8G8B8A8_UNORM);color(device,DXGI_FORMAT_R8G8B8A8_TYPELESS);depth(device);msaa(device);std::cout<<"NativeOuterTexture tests passed\n";return 0;}
catch(const std::exception& e){std::cerr<<"NativeOuterTexture: "<<e.what()<<"\n";return 1;}}
