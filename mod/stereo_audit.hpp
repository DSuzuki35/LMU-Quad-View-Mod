#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>
#include <cstddef>
namespace lmuqv {
inline constexpr std::array<unsigned,24> stereo_audit_vtable{12,13,20,21,38,39,40,41,42,14,15,48,47,46,12,15,13,16,17,18,11,58,27,28};
enum class StereoAuditPass:unsigned {outer0,outer1,focus0,focus1,none};
inline StereoAuditPass stereo_audit_eye(bool focus,unsigned eye){return StereoAuditPass((focus?2:0)+eye);}
struct StereoAuditToken {bool entered=false;unsigned slot=0;long long started=0;};
struct StereoNativePrepareToken {bool entered=false;unsigned slot=0;long long started=0;uint64_t generation=0;unsigned metadataIndex=~0u;};
struct StereoNativePrepareInfo {
    const void* shader=nullptr;
    uint32_t suppliedFlags=0;
    bool reset=false;
    bool(*readable)(const void*,size_t)=nullptr;
};
struct StereoNativePassInfo {
    const void* scene=nullptr;
    const void* view=nullptr;
    uint32_t callerRva=0;
    uint32_t flags=0;
    bool lastMainPass=false;
    bool(*readable)(const void*,size_t)=nullptr;
};
struct StereoNativePassToken {bool entered=false;unsigned index=0;uint64_t generation=0;};
StereoNativePassToken stereo_audit_native_pass_begin(const StereoNativePassInfo&);
void stereo_audit_native_pass_end(const StereoNativePassToken&);
StereoNativePrepareToken stereo_audit_native_prepare_begin(const void* mesh,const StereoNativePrepareInfo* info=nullptr);
void stereo_audit_native_prepare_end(const StereoNativePrepareToken&);
bool stereo_audit_configure(ID3D11DeviceContext*,bool,const wchar_t* directory,void(*log)(const char*));
// Arm before the game creates its D3D11 device, to observe input layouts and
// shaders that already exist by xrCreateSession. Only main-executable factory
// calls qualify; runtime/provider helper devices are ignored.
bool stereo_audit_prepare(bool,const wchar_t* directory,void(*log)(const char*));
bool stereo_audit_enabled();
void stereo_audit_frame(bool keyDown);
void stereo_audit_frame_end();
StereoAuditToken stereo_audit_begin(StereoAuditPass);
void stereo_audit_end(StereoAuditToken&);
int stereo_audit_group(int group);
void stereo_audit_stop();
class StereoAuditScope {
    StereoAuditToken token_;
public:
    explicit StereoAuditScope(StereoAuditPass pass):token_(stereo_audit_begin(pass)){}
    ~StereoAuditScope(){end();}
    void end(){stereo_audit_end(token_);}
    StereoAuditScope(const StereoAuditScope&)=delete;
    StereoAuditScope& operator=(const StereoAuditScope&)=delete;
};
class StereoAuditGroup {
    int previous_;
public:
    explicit StereoAuditGroup(int group):previous_(stereo_audit_group(group)){}
    ~StereoAuditGroup(){stereo_audit_group(previous_);}
    StereoAuditGroup(const StereoAuditGroup&)=delete;
    StereoAuditGroup& operator=(const StereoAuditGroup&)=delete;
};
}
