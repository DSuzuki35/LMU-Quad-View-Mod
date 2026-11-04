#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>

namespace lmuqv {
class StereoAuditTrigger {
    std::filesystem::path path_;
    uint64_t timestamp_=0,lastPoll_=0;
    bool armed_=false,observed_=false;
    static bool attributes(const wchar_t* path,WIN32_FILE_ATTRIBUTE_DATA& data){
        return GetFileAttributesExW(path,GetFileExInfoStandard,&data)!=FALSE;
    }
    template<class Probe> bool observe(Probe probe){
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if(!probe(path_.c_str(),data)||(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return false;
        const uint64_t timestamp=(uint64_t(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime;
        const bool changed=!observed_||timestamp_!=timestamp;
        observed_=true;timestamp_=timestamp;return changed;
    }
public:
    void reset(){armed_=observed_=false;timestamp_=lastPoll_=0;path_.clear();}
    template<class Clock, class Probe>
    void configure(bool enabled,const std::filesystem::path& directory,Clock clock,Probe probe){
        reset();if(!enabled)return;
        path_=directory/L"stereo-audit.request";
        lastPoll_=clock();observe(probe);armed_=true;
    }
    template<class Clock, class Probe>
    bool poll(Clock clock,Probe probe){
        if(!armed_)return false;
        const uint64_t now=clock();if(now-lastPoll_<500)return false;
        lastPoll_=now;return observe(probe);
    }
    void configure(bool enabled,const std::filesystem::path& directory){
        configure(enabled,directory,[]{return GetTickCount64();},attributes);
    }
    bool poll(){return poll([]{return GetTickCount64();},attributes);}
};
}
