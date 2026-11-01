#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../stereo_audit_trigger.hpp"
#include <iostream>
#include <string>

namespace {
unsigned failures=0;
void expect(bool okay,const char* name){if(!okay){std::cerr<<"FAIL: "<<name<<'\n';++failures;}}
struct Fixture {
    lmuqv::StereoAuditTrigger trigger;
    uint64_t now=1000, stamp=0;
    unsigned clocks=0,probes=0;
    bool exists=false,directory=false;
    std::wstring expected=L"C:\\audit\\stereo-audit.request";
    auto clock(){return [this]{++clocks;return now;};}
    auto probe(){return [this](const wchar_t* path,WIN32_FILE_ATTRIBUTE_DATA& data){
        ++probes;expect(path==expected,"only fixed configured request path is probed");
        data.dwFileAttributes=directory?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL;
        data.ftLastWriteTime.dwLowDateTime=DWORD(stamp);
        data.ftLastWriteTime.dwHighDateTime=DWORD(stamp>>32);
        return exists;
    };}
    void arm(){trigger.configure(true,L"C:\\audit",clock(),probe());}
    bool poll(){return trigger.poll(clock(),probe());}
    bool after(uint64_t ms){now+=ms;return poll();}
};
void absent_then_changed(){
    Fixture f;f.arm();expect(f.probes==1,"initial absence is sampled");
    f.exists=true;f.stamp=0;expect(f.after(500),"new file including zero timestamp triggers");
    expect(!f.after(500),"unchanged marker cannot repeat");
    f.stamp=1ull<<32;expect(f.after(500),"high FILETIME bits cause a new request");
}
void stale_and_transient(){
    Fixture f;f.exists=true;f.stamp=123;f.arm();
    expect(!f.after(500),"existing marker at initialization is stale");
    f.exists=false;expect(!f.after(500),"absence or failed attributes cannot trigger");
    f.exists=true;expect(!f.after(500),"transient absence cannot replay consumed timestamp");
    f.stamp=124;expect(f.after(500),"changed timestamp triggers once");
    f.exists=false;expect(!f.after(500),"failed read after consumed request cannot trigger");
    f.exists=true;expect(!f.after(500),"failed read cannot forget consumed request");
}
void rate_limit(){
    Fixture f;f.arm();f.exists=true;f.stamp=8;
    expect(!f.after(499)&&f.probes==1,"no probe before 500ms including after initialization");
    expect(f.after(1)&&f.probes==2,"exactly 500ms allows a probe");
    f.exists=false;expect(!f.after(500)&&f.probes==3,"failed poll is still rate limited");
    expect(!f.after(499)&&f.probes==3,"failure does not bypass poll throttle");
}
void directories(){
    Fixture f;f.exists=true;f.directory=true;f.stamp=20;f.arm();
    expect(!f.after(500),"directory marker is refused");
    f.directory=false;expect(f.after(500),"directory timestamp was not consumed as a file");
    f.directory=true;f.stamp=21;expect(!f.after(500),"changed directory cannot trigger");
    f.directory=false;f.stamp=20;expect(!f.after(500),"directory cannot replace consumed file timestamp");
}
void reset_and_reconfigure(){
    Fixture f;expect(!f.poll()&&f.clocks==0&&f.probes==0,"unarmed poll performs no clock or probe");
    f.trigger.configure(false,L"C:\\audit",f.clock(),f.probe());
    expect(!f.poll()&&f.clocks==0&&f.probes==0,"disabled configuration performs no clock or probe");
    f.exists=true;f.stamp=40;f.arm();f.stamp=41;expect(f.after(500),"first configuration triggers");
    f.trigger.reset();const auto clocks=f.clocks,probes=f.probes;
    expect(!f.poll()&&f.clocks==clocks&&f.probes==probes,"reset disarms without clock or probe");
    f.expected=L"C:\\other\\stereo-audit.request";
    f.trigger.configure(true,L"C:\\other",f.clock(),f.probe());
    expect(!f.after(500),"new directory is independently primed");
    f.stamp=42;expect(f.after(500),"new directory can trigger after priming");
    f.trigger.configure(false,L"C:\\other",f.clock(),f.probe());
    const auto disabledClocks=f.clocks,disabledProbes=f.probes;
    expect(!f.after(500)&&f.clocks==disabledClocks&&f.probes==disabledProbes,"disabled reconfiguration disarms");
}
}
int main(){absent_then_changed();stale_and_transient();rate_limit();directories();reset_and_reconfigure();
    if(failures)return 1;
    std::cout<<"Stereo audit trigger passed: stale/absent/changed markers, FILETIME, failures, directories, 500ms throttle, reset and disabled no-I/O\n";
}
