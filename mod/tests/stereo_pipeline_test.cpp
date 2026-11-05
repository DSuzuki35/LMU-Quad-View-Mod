#include "stereo_test_fixture.hpp"
#include "../stereo_pipeline.hpp"
#include <thread>
using namespace lmuqv;
int main(){
    Fixture f(false);StereoPipelineCache cache;assert(cache.configure(f.d,true));
    auto* v=compile(vsSource,"vs_4_0");auto* p=compile(psSource,"ps_4_0");
    std::vector<uint8_t> vs(static_cast<uint8_t*>(v->GetBufferPointer()),static_cast<uint8_t*>(v->GetBufferPointer())+v->GetBufferSize());
    std::vector<uint8_t> ps(static_cast<uint8_t*>(p->GetBufferPointer()),static_cast<uint8_t*>(p->GetBufferPointer())+p->GetBufferSize());drop(v);drop(p);
    std::vector<StereoInputElement> layout{{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0},{"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12}};
    auto program=cache.get(vs,ps,17,8,layout);if(!program)std::cerr<<cache.reason()<<'\n';assert(program&&program->geometry&&!program->direct&&cache.size()==1);
    assert(program->vertex.constantRegisters[0]==5&&program->vertex.constantRegisters[4]==1&&program->pixel.constantRegisters[3]==1);
    assert(cache.get(vs,ps,17,8,layout)==program&&cache.configure(f.d,true)&&cache.get(vs,ps,17,8,layout)==program);
    auto instance=layout;instance[1].slot=1;instance[1].offset=0;instance[1].classification=D3D11_INPUT_PER_INSTANCE_DATA;instance[1].step=2;
    auto instanced=cache.get(vs,ps,17,8,instance);assert(instanced&&instanced->inputSlots==3&&cache.size()==2);
    instance[1].step=0xffffffffu;assert(!cache.get(vs,ps,17,8,instance)&&cache.size()==2);
    auto broken=vs;broken.back()^=1;assert(!cache.get(broken,ps,17,8,layout)&&cache.size()==2);
    assert(!cache.get(vs,ps,0,8,layout));bool foreign=true;std::thread worker([&]{foreign=bool(cache.get(vs,ps,17,8,layout));});worker.join();assert(!foreign);
    assert(cache.configure(f.d));program=cache.get(vs,ps,17,8,layout);assert(program);
    assert(program->direct==query_stereo_capabilities(f.d).vertexArrayIndex);
    std::cout<<"Stereo pipeline WARP: cached original/converted shaders, both routes, owned layouts and declarations, instance-rate and refusal guards passed\n";
}
