#include "stereo_test_fixture.hpp"
#include "../stereo_draw_proof.hpp"
#include <thread>

namespace {
void waitQuery(ID3D11DeviceContext* c,ID3D11Query* query,void* data,UINT bytes){
    HRESULT hr=S_FALSE;const auto deadline=GetTickCount64()+5000;
    while((hr=c->GetData(query,data,bytes,0))==S_FALSE&&GetTickCount64()<deadline)Sleep(1);
    assert(hr==S_OK);
}
void bind(Fixture& f,unsigned eye){
    auto* c=f.c;const UINT stride=24,offset=0;c->IASetVertexBuffers(0,1,&f.vertices,&stride,&offset);c->IASetIndexBuffer(f.indices,DXGI_FORMAT_R32_UINT,0);c->IASetInputLayout(f.layout);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(f.referenceVs,nullptr,0);c->PSSetShader(f.referencePs,nullptr,0);c->GSSetShader(nullptr,nullptr,0);c->RSSetState(f.raster);D3D11_VIEWPORT v{0,0,float(Fixture::width),float(Fixture::width),0,1};c->RSSetViewports(1,&v);c->OMSetDepthStencilState(f.depthState,0);c->OMSetRenderTargets(1,&f.eyeRt[eye],f.eyeDs[eye]);
    c->UpdateSubresource(f.camera,0,nullptr,&f.cameras[eye],0,0);c->UpdateSubresource(f.extra,0,nullptr,f.extras[eye],0,0);c->UpdateSubresource(f.pixel,0,nullptr,f.scales[eye],0,0);c->VSSetConstantBuffers(0,1,&f.camera);c->VSSetConstantBuffers(4,1,&f.extra);c->PSSetConstantBuffers(3,1,&f.pixel);
    c->CSSetConstantBuffers(5,1,&f.pixel); // A stage the replay must leave untouched.
}
void checkState(Fixture& f,unsigned eye){
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;f.c->VSGetShader(&vs,nullptr,nullptr);f.c->PSGetShader(&ps,nullptr,nullptr);assert(vs==f.referenceVs&&ps==f.referencePs);drop(vs);drop(ps);
    ID3D11RenderTargetView* target=nullptr;ID3D11DepthStencilView* depth=nullptr;f.c->OMGetRenderTargets(1,&target,&depth);assert(target==f.eyeRt[eye]&&depth==f.eyeDs[eye]);drop(target);drop(depth);
    ID3D11Buffer *original=nullptr,*packed=nullptr,*compute=nullptr;f.c->VSGetConstantBuffers(0,1,&original);f.c->VSGetConstantBuffers(13,1,&packed);f.c->CSGetConstantBuffers(5,1,&compute);assert(original==f.camera&&!packed&&compute==f.pixel);drop(original);drop(compute);
}
std::vector<uint8_t> nativePixels(Fixture& f){
    D3D11_TEXTURE2D_DESC d{};f.color->GetDesc(&d);const auto stride=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT?16:4;d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(f.d->CreateTexture2D(&d,nullptr,&staging)));f.c->CopyResource(staging,f.color);
    std::vector<uint8_t> values(size_t(d.Width)*d.Height*2*stride);for(unsigned eye=0;eye<2;++eye){D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(f.c->Map(staging,eye,D3D11_MAP_READ,0,&mapped)));for(unsigned y=0;y<d.Height;++y)std::memcpy(values.data()+size_t(eye*d.Height+y)*d.Width*stride,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(d.Width)*stride);f.c->Unmap(staging,eye);}drop(staging);return values;
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);std::cout<<std::unitbuf;
    bool hardware=false;for(int i=1;i<argc;++i)hardware|=!std::strcmp(argv[i],"--hardware");
    auto* vertex=compile(vsSource,"vs_4_0");auto* pixel=compile(psSource,"ps_4_0");
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT})for(bool geometry:{false,true}){
        Fixture f(hardware,format);const float marker[4]{.75f,.25f,.5f,1};f.c->ClearRenderTargetView(f.rt,marker);const auto before=nativePixels(f);bind(f,0);
        lmuqv::StereoProofDraw draw;draw.key=91;draw.indexCount=6;draw.vertexCode=vertex->GetBufferPointer();draw.vertexBytes=vertex->GetBufferSize();draw.pixelCode=pixel->GetBufferPointer();draw.pixelBytes=pixel->GetBufferSize();draw.vertexConstants=1|(1u<<4);draw.pixelConstants=1u<<3;draw.forceGeometry=geometry;
        lmuqv::StereoDrawProof proof;proof.begin();ID3D11Query *timer=nullptr,*occlusion=nullptr;
        D3D11_QUERY_DESC queryDesc{D3D11_QUERY_TIMESTAMP_DISJOINT,0};assert(SUCCEEDED(f.d->CreateQuery(&queryDesc,&timer)));f.c->Begin(timer);
        queryDesc.Query=D3D11_QUERY_OCCLUSION;assert(SUCCEEDED(f.d->CreateQuery(&queryDesc,&occlusion)));f.c->Begin(occlusion);
        // Begin/End tracking in the caller must withhold extra replay draws
        // during result-counting query scopes, even alongside a timing query.
        // The occlusion result must stay empty: no diagnostic draws or snapshots.
        proof.observe(f.c,false,draw,false,true);assert(proof.result().status=="armed"&&proof.result().querySkips==1&&proof.result().timingQueryOverlaps==0);
        f.c->End(occlusion);UINT64 samples=~UINT64(0);waitQuery(f.c,occlusion,&samples,sizeof(samples));assert(samples==0);drop(occlusion);
        proof.observe(f.c,false,draw,true,true);if(proof.result().status!="waiting_right")std::cerr<<proof.result().reason<<'\n';assert(proof.result().status=="waiting_right");checkState(f,0);
        // The SAME native buffers now contain the right-eye values. Retaining
        // only their pointers would corrupt the original left-eye reference.
        bind(f,1);proof.observe(f.c,true,draw,true,true);const auto result=proof.result();if(result.status!="passed")std::cerr<<result.status<<": "<<result.reason<<" differences="<<result.colorDifferent[0]<<'/'<<result.colorDifferent[1]<<" depth="<<result.depthDifferent[0]<<'/'<<result.depthDifferent[1]<<'\n';assert(result.status=="passed"&&result.covered[0]>100&&result.covered[1]>100&&result.timingQueryOverlaps==2);checkState(f,1);assert(before==nativePixels(f));proof.finish();
        f.c->End(timer);D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{};waitQuery(f.c,timer,&timing,sizeof(timing));assert(timing.Frequency>0);drop(timer);
        std::cout<<"Private stereo proof format="<<unsigned(format)<<" route="<<result.route<<": GPU CB snapshots/packing, separate eye data, color/depth, native targets and context restoration PASS\n";
        // A mismatch in a high texture slot must be rejected even when a simple
        // shader happens not to sample it. No first-16-only sharing assumption.
        bind(f,0);proof.begin();proof.observe(f.c,false,draw,true);bind(f,1);
        D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;ID3D11Texture2D* t=nullptr;ID3D11ShaderResourceView* srv=nullptr;assert(SUCCEEDED(f.d->CreateTexture2D(&td,nullptr,&t)));assert(SUCCEEDED(f.d->CreateShaderResourceView(t,nullptr,&srv)));f.c->PSSetShaderResources(20,1,&srv);
        proof.observe(f.c,true,draw,true);assert(proof.result().status=="unavailable"&&proof.result().reason.find("SRV")!=std::string::npos);checkState(f,1);proof.finish();ID3D11ShaderResourceView* empty=nullptr;f.c->PSSetShaderResources(20,1,&empty);drop(srv);drop(t);
        bind(f,0);proof.begin();proof.observe(f.c,false,draw,true);auto different=draw;different.key=92;bind(f,1);proof.observe(f.c,true,different,true);proof.finish();assert(proof.result().status=="inconclusive"&&proof.result().reason=="no matching right-eye draw");
        proof.begin();bind(f,0);proof.observe(f.c,false,draw,true);std::thread close([&]{proof.finish();});close.join();assert(proof.result().status=="inconclusive");
    }
    drop(vertex);drop(pixel);std::cout<<"Stereo private-draw proof "<<(hardware?"hardware":"WARP")<<" passed: no game draw suppression; timing scopes allowed, empty occlusion result preserved, high texture slots, unmatched eye and cross-thread release\n";
}
