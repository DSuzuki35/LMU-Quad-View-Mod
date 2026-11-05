// Writes a benchmark ABI v1 image produced by the mod's own writer functions.
#include "../../mod/benchmark_native.hpp"
#include <cstdio>
#include <memory>
using namespace lmuqv::benchmark;
int main(int argc,char** argv){
    if(argc!=2)return 2;
    auto data=std::make_unique<Shared>();
    initialize(*data,4242,10000000);
    auto& h=data->header;
    h.running=1;h.status=2;h.projections=4;h.result=0;h.visible=1;h.recording=0;
    h.outer0Width=1200;h.outer0Height=1100;h.outer1Width=1200;h.outer1Height=1100;h.focusWidth=1600;h.focusHeight=1500;
    FocusTelemetry focus;focus.flags=1;focus.leftHorizontal=48.5f;focus.leftVertical=40.25f;focus.rightHorizontal=48.5f;focus.rightVertical=40.25f;focus.frame=180;
    publish_focus(*data,focus);
    for(int i=1;i<=200;i++){
        Sample s{};s.qpc=int64_t(i)*111111;s.frame=uint64_t(i);s.intervalMs=i==150?25.0:11.1111;s.appMs=3.25;s.providerMs=1.5;s.gpuMs=7.75;
        s.flags=(i==120?0u:1u)|(i%2==0?8u:0u);s.result=0;publish(*data,s);
    }
    FILE* f=std::fopen(argv[1],"wb");if(!f)return 3;
    const bool ok=std::fwrite(data.get(),sizeof(Shared),1,f)==1;std::fclose(f);return ok?0:4;
}
