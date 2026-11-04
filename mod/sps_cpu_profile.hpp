#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace lmuqv {
enum class SpsCpuWork : unsigned { preparation, transform, other_transform, upload, draw, map, unmap, count };

// Sampled cost attribution, never a replacement/eligibility decision. Clock
// cycles remain cycles: QueryThreadCycleTime must not be converted to time.
// All mutable state is protected; no lock spans a native/driver invocation.
template<class Clock, unsigned Capacity=2048>
class BasicSpsCpuProfile {
public:
    static constexpr unsigned WorkCount=unsigned(SpsCpuWork::count);
    struct Work { uint64_t calls=0,inclusive=0,exclusive=0; };
    struct Sample {
        uint64_t wall=0,cycles=0,remainder=0;
        bool cyclesAvailable=false;
        std::array<Work,WorkCount> work{};
    };
    struct Token { uint64_t generation=0,range=0;unsigned depth=0;bool entered=false; };
    struct RangeToken { uint64_t generation=0,range=0;bool entered=false; };
    using Log=void(*)(const char*);

    void configure(bool enable,Log log,unsigned seconds=120) {
        enabled_.store(false);std::lock_guard<std::mutex> lock(mutex_);
        active_=false;detailedRange_.store(false);owner_.store(0);logger_=log;keyDown_=false;sampleFrame_=false;
        ++generation_;rangeDepth_=stackDepth_=0;
        frequency_=enable?Clock::frequency():0;seconds_=seconds;
        enabled_.store(frequency_!=0);
        if(enabled())emit("sps_cpu_profile armed hotkey=Ctrl+Alt+F10 sample_every=16 frames alternating=coarse,detailed; all native work retained");
    }
    bool enabled()const {return enabled_.load(std::memory_order_relaxed);}
    void frame(bool keyDown) {
        if(!enabled())return;
        unsigned empty=0;owner_.compare_exchange_strong(empty,Clock::thread());
        if(!owns())return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!enabled())return;
        const bool pressed=keyDown&&!keyDown_;keyDown_=keyDown;
        const auto now=Clock::now();
        if(active_&&(pressed||now-started_>=uint64_t(seconds_)*frequency_)) {
            finish_locked(pressed?"hotkey":"duration");return;
        }
        if(!active_&&pressed) {
            for(auto& phase:buckets_)for(auto& b:phase){b.count=b.dropped=0;}
            ++generation_;frames_=sampledFrames_=invalid_=cycleFailures_=0;
            started_=now;active_=true;rangeDepth_=stackDepth_=0;
            emit("sps_cpu_profile start rendering=legacy unit=wall_ms_per_group cycles_unit=thread_cycles; snapshots=0 readbacks=0 native_work_removed=0");
        }
        if(!active_)return;
        if(rangeDepth_){++invalid_;rangeDepth_=stackDepth_=0;++rangeSerial_;}
        sampleFrame_=(frames_++%16)==0;
        if(sampleFrame_){detailed_=(sampledFrames_++%2)!=0;}
    }
    // Invoke for every world-sort range on the owner thread. A nested or
    // unverified range cannot accidentally inherit its parent's attribution.
    RangeToken range_begin(unsigned slot,bool admitted) {
        if(!owns())return {};
        std::lock_guard<std::mutex> lock(mutex_);
        if(!enabled()||!active_||!sampleFrame_)return {};
        ++rangeDepth_;
        if(rangeDepth_>1){valid_=false;detailedRange_.store(false);return {generation_,rangeSerial_,true};}
        ++rangeSerial_;valid_=admitted&&slot<4;slot_=admitted?slot:4;stackDepth_=0;current_={};
        if(valid_){cycleStartAvailable_=Clock::cycles(cycleStart_);rangeStart_=Clock::now();}
        detailedRange_.store(valid_&&detailed_,std::memory_order_relaxed);
        return {generation_,rangeSerial_,true};
    }
    void range_end(RangeToken& token) {
        if(!token.entered||!owns())return;
        std::lock_guard<std::mutex> lock(mutex_);token.entered=false;
        if(token.generation!=generation_||token.range!=rangeSerial_||!rangeDepth_)return;
        if(--rangeDepth_)return;
        detailedRange_.store(false,std::memory_order_relaxed);
        if(!valid_||stackDepth_){if(slot_<4)++invalid_;return;}
        const auto end=Clock::now();uint64_t cycles=0;
        const bool available=Clock::cycles(cycles);
        if(end<rangeStart_){++invalid_;return;}
        current_.wall=end-rangeStart_;
        current_.cyclesAvailable=cycleStartAvailable_&&available&&cycles>=cycleStart_;
        if(current_.cyclesAvailable)current_.cycles=cycles-cycleStart_;else ++cycleFailures_;
        uint64_t attributed=0;for(const auto& w:current_.work)attributed+=w.exclusive;
        if(attributed>current_.wall){++invalid_;return;}
        current_.remainder=current_.wall-attributed;
        auto& bucket=buckets_[unsigned(detailed_)][slot_];
        if(bucket.count<Capacity)bucket.samples[bucket.count++]=current_;else ++bucket.dropped;
    }
    Token begin(SpsCpuWork work) {
        if(!detailedRange_.load(std::memory_order_relaxed)||!owns())return {};
        std::lock_guard<std::mutex> lock(mutex_);
        if(!enabled()||!active_||!sampleFrame_||!detailed_||rangeDepth_!=1||!valid_||unsigned(work)>=WorkCount)return {};
        if(stackDepth_==stack_.size()){valid_=false;return {};}
        stack_[stackDepth_]={Clock::now(),0,work};++stackDepth_;
        return {generation_,rangeSerial_,stackDepth_,true};
    }
    void end(Token& token) {
        if(!token.entered||!owns())return;
        std::lock_guard<std::mutex> lock(mutex_);token.entered=false;
        if(token.generation!=generation_||token.range!=rangeSerial_||!rangeDepth_)return;
        if(token.depth!=stackDepth_){valid_=false;return;}
        const auto end=Clock::now();const auto entry=stack_[--stackDepth_];
        if(end<entry.begin||end-entry.begin<entry.children){valid_=false;return;}
        const auto elapsed=end-entry.begin;auto& w=current_.work[unsigned(entry.work)];
        ++w.calls;w.inclusive+=elapsed;w.exclusive+=elapsed-entry.children;
        if(stackDepth_)stack_[stackDepth_-1].children+=elapsed;
    }
    void stop(const char* reason="session_end") {
        enabled_.store(false);std::lock_guard<std::mutex> lock(mutex_);
        finish_locked(reason);owner_.store(0);++generation_;
    }
    unsigned samples(bool detailed,unsigned slot)const {
        std::lock_guard<std::mutex> lock(mutex_);return buckets_[unsigned(detailed)][slot].count;
    }
    Sample sample(bool detailed,unsigned slot,unsigned index)const {
        std::lock_guard<std::mutex> lock(mutex_);return buckets_[unsigned(detailed)][slot].samples[index];
    }
private:
    struct Bucket {std::array<Sample,Capacity> samples{};unsigned count=0,dropped=0;};
    struct Entry {uint64_t begin=0,children=0;SpsCpuWork work=SpsCpuWork::count;};
    std::array<std::array<Bucket,4>,2> buckets_{};
    std::array<Entry,32> stack_{};
    Sample current_{};
    std::atomic<bool> enabled_{false},detailedRange_{false};std::atomic<unsigned> owner_{0};mutable std::mutex mutex_;
    Log logger_=nullptr;
    bool active_=false,keyDown_=false,sampleFrame_=false,detailed_=false,valid_=false,cycleStartAvailable_=false;
    unsigned seconds_=120,rangeDepth_=0,stackDepth_=0,slot_=4;
    uint64_t frequency_=0,started_=0,generation_=0,frames_=0,sampledFrames_=0;
    uint64_t rangeSerial_=0,rangeStart_=0,cycleStart_=0,invalid_=0,cycleFailures_=0;
    bool owns()const {return enabled()&&owner_.load(std::memory_order_relaxed)==Clock::thread();}
    void emit(const char* line){if(logger_)logger_(line);}
    void finish_locked(const char* reason) {
        if(!active_)return;
        active_=false;sampleFrame_=false;detailedRange_.store(false);
        if(rangeDepth_){++invalid_;rangeDepth_=stackDepth_=0;}
        char line[768]{};
        std::snprintf(line,sizeof(line),"sps_cpu_profile summary reason=%s scene_frames=%llu sampled_frames=%llu invalid_ranges=%llu cycle_failures=%llu frequency=%llu; wall includes waits/instrumentation; cycles are not milliseconds; coarse/detailed are different frames",reason,
            static_cast<unsigned long long>(frames_),static_cast<unsigned long long>(sampledFrames_),static_cast<unsigned long long>(invalid_),static_cast<unsigned long long>(cycleFailures_),static_cast<unsigned long long>(frequency_));emit(line);
        constexpr const char* slots[]={"outer_left","outer_right","focus_left","focus_right"};
        constexpr const char* names[]={"preparation","transform","other_transform","upload","draw","map","unmap"};
        for(unsigned phase=0;phase<2;++phase)for(unsigned slot=0;slot<4;++slot) {
            const auto& b=buckets_[phase][slot];if(!b.count)continue;
            std::array<uint64_t,Capacity> walls{};uint64_t sum=0,cycles=0,remainder=0,cycleSamples=0;
            std::array<Work,WorkCount> work{};
            for(unsigned i=0;i<b.count;++i){const auto& s=b.samples[i];walls[i]=s.wall;sum+=s.wall;remainder+=s.remainder;
                if(s.cyclesAvailable){cycles+=s.cycles;++cycleSamples;}
                for(unsigned j=0;j<WorkCount;++j){work[j].calls+=s.work[j].calls;work[j].inclusive+=s.work[j].inclusive;work[j].exclusive+=s.work[j].exclusive;}}
            std::sort(walls.begin(),walls.begin()+b.count);
            const double scale=1000.0/double(frequency_);const auto mean=[&](uint64_t ticks){return double(ticks)*scale/b.count;};
            const auto percentile=[&](unsigned p){return double(walls[(b.count*p+99)/100-1])*scale;};
            std::snprintf(line,sizeof(line),"sps_cpu_profile group phase=%s slot=%s samples=%u dropped=%u wall_mean_ms=%.6f wall_p95_ms=%.6f wall_p99_ms=%.6f thread_cycles_mean=%.3f cycle_samples=%llu remainder_mean_ms=%.6f",
                phase?"detailed":"coarse",slots[slot],b.count,b.dropped,mean(sum),percentile(95),percentile(99),cycleSamples?double(cycles)/double(cycleSamples):0.0,static_cast<unsigned long long>(cycleSamples),mean(remainder));emit(line);
            if(phase)for(unsigned j=0;j<WorkCount;++j){std::snprintf(line,sizeof(line),"sps_cpu_profile work slot=%s kind=%s calls=%llu inclusive_mean_ms=%.6f exclusive_mean_ms=%.6f",
                slots[slot],names[j],static_cast<unsigned long long>(work[j].calls),mean(work[j].inclusive),mean(work[j].exclusive));emit(line);}
        }
        emit("sps_cpu_profile complete; exclusive categories plus remainder partition detailed group wall; inclusive categories overlap; no eligibility or speedup established");
    }
};
}
