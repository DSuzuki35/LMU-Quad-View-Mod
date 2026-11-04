#include "../sps_cpu_profile.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
struct Clock {
    inline static uint64_t ticks=0,spent=0,reads=0,cycleReads=0;
    inline static unsigned id=1;inline static bool cycleOk=true;
    static uint64_t frequency(){return 1000;}
    static uint64_t now(){++reads;return ticks;}
    static unsigned thread(){return id;}
    static bool cycles(uint64_t& value){++cycleReads;value=spent;return cycleOk;}
};
std::vector<std::string> lines;
void log(const char* line){lines.emplace_back(line);}
bool contains(const char* text){for(const auto& line:lines)if(line.find(text)!=std::string::npos)return true;return false;}
}
int main(){
    using namespace lmuqv;
    using Profile=BasicSpsCpuProfile<Clock,2>;
    auto p=std::make_unique<Profile>();
    p->configure(false,log);p->frame(true);auto ignored=p->range_begin(2,true);
    auto noWork=p->begin(SpsCpuWork::draw);p->end(noWork);p->range_end(ignored);
    assert(Clock::reads==0&&Clock::cycleReads==0&&lines.empty());
    p->configure(true,log,30);p->frame(true); // first sampled frame: coarse
    auto coarse=p->range_begin(2,true);Clock::ticks+=10;Clock::spent+=100;
    noWork=p->begin(SpsCpuWork::draw);assert(!noWork.entered);p->range_end(coarse);
    assert(p->samples(false,2)==1&&p->sample(false,2,0).wall==10&&p->sample(false,2,0).cycles==100);
    for(unsigned i=0;i<16;++i)p->frame(false); // detailed
    auto range=p->range_begin(2,true);Clock::ticks+=1;
    auto prepare=p->begin(SpsCpuWork::preparation);Clock::ticks+=2;
    auto transform=p->begin(SpsCpuWork::transform);Clock::ticks+=3;p->end(transform);
    Clock::ticks+=4;p->end(prepare);p->end(prepare);
    auto upload=p->begin(SpsCpuWork::upload);Clock::ticks+=1;
    auto map=p->begin(SpsCpuWork::map);Clock::ticks+=2;p->end(map);
    Clock::ticks+=1;p->end(upload);
    auto draw=p->begin(SpsCpuWork::draw);Clock::ticks+=5;p->end(draw);
    Clock::ticks+=1;Clock::spent+=500;p->range_end(range);
    const auto sample=p->sample(true,2,0);
    assert(sample.wall==20&&sample.remainder==2&&sample.cycles==500);
    assert(sample.work[0].inclusive==9&&sample.work[0].exclusive==6&&sample.work[0].calls==1);
    assert(sample.work[1].inclusive==3&&sample.work[3].exclusive==2&&sample.work[5].exclusive==2);
    uint64_t partition=sample.remainder;for(const auto& w:sample.work)partition+=w.exclusive;assert(partition==sample.wall);
    // Other threads and stale/cross-frame tokens cannot write a sample.
    Clock::id=2;range=p->range_begin(3,true);assert(!range.entered);
    noWork=p->begin(SpsCpuWork::draw);assert(!noWork.entered);p->frame(true);Clock::id=1;
    range=p->range_begin(2,true);auto nested=p->range_begin(4,false);
    noWork=p->begin(SpsCpuWork::draw);assert(!noWork.entered);
    p->range_end(nested);p->range_end(range);assert(p->samples(true,2)==1);
    range=p->range_begin(2,true);draw=p->begin(SpsCpuWork::draw);p->frame(false);
    p->end(draw);p->range_end(range);assert(p->samples(true,2)==1);
    // A failed cycle query still yields a wall sample with explicit coverage.
    for(unsigned i=0;i<15;++i)p->frame(false);
    Clock::cycleOk=false;range=p->range_begin(3,true);Clock::ticks+=2;p->range_end(range);
    assert(p->samples(false,3)==1&&!p->sample(false,3,0).cyclesAvailable);
    Clock::cycleOk=true;
    // Bounded storage and a duration save, including diagnostics on another thread.
    for(unsigned i=0;i<3;++i){range=p->range_begin(3,true);p->range_end(range);}
    Clock::id=2;p->stop();Clock::id=1;
    assert(contains("invalid_ranges=2")&&contains("cycle_failures=1"));
    assert(contains("slot=focus_right samples=2 dropped=2"));
    assert(contains("kind=preparation calls=1 inclusive_mean_ms=9.000000 exclusive_mean_ms=6.000000"));
    assert(!p->enabled());
    p->configure(true,log,1);p->frame(true);range=p->range_begin(0,true);
    draw=p->begin(SpsCpuWork::draw);p->stop();p->end(draw);p->range_end(range);
    p->configure(true,log,1);p->frame(true);Clock::ticks+=1000;p->frame(false);
    assert(contains("reason=duration"));p->stop();
    std::cout<<"SPS CPU profile: nested accounting, coarse controls, thread ownership, stale/invalid ranges, capacity, failed cycle queries and teardown passed\n";
}
