#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include "native_stereo_classify_capture.hpp"
#include "native_stereo_transform.hpp"
#define LMU_STEREO_TRANSFORM_AUDIT 1
namespace lmuqv {
struct StereoNativeTransformInfo {
    NativeStereoTransformArgs args;
    bool(*readable)(const void*,size_t)=nullptr;
};
struct StereoNativeTransformToken {
    bool entered=false;unsigned index=0,slot=0;uint64_t generation=0,scopeEpoch=0;
    StereoNativeTransformInfo invocation;
};
StereoNativeTransformToken stereo_audit_native_transform_begin(const StereoNativeTransformInfo&,unsigned entryMxcsr) noexcept;
void stereo_audit_native_transform_end(const StereoNativeTransformToken&,unsigned postMxcsr) noexcept;
void stereo_audit_native_transform_hook_available(bool) noexcept;
inline constexpr std::array<unsigned,25> stereo_audit_vtable{12,13,20,21,38,39,40,41,42,14,15,48,47,46,12,15,13,16,17,18,11,58,27,28,57};
enum class StereoAuditPass:unsigned {outer0,outer1,focus0,focus1,none};
inline StereoAuditPass stereo_audit_eye(bool focus,unsigned eye){return StereoAuditPass((focus?2:0)+eye);}
struct StereoAuditToken {bool entered=false;unsigned slot=0;long long started=0;uint64_t generation=0;};
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
    // Optional addresses only; bounded capture reads them inside the active
    // owner-thread group-31 gate. Existing aggregate initializers remain valid.
    const void* cameraState=nullptr;
    const void* cameraMode=nullptr;
    const void* currentView=nullptr;
};
struct StereoNativePassToken {bool entered=false;unsigned index=0;uint64_t generation=0;};
struct StereoNativeClassifyInfo {
  NativeStereoClassifyCaptureArgs args;
  uint32_t callerRva = 0;
  bool (*readable)(const void *, size_t) = nullptr;
};
struct StereoNativeClassifyToken {
  bool entered = false;
  unsigned index = 0, slot = 0;
  uint64_t generation = 0;
  StereoNativeClassifyInfo invocation;
  uintptr_t positionIdentity = 0, ownerIdentity = 0;
};
StereoNativeClassifyToken stereo_audit_native_classify_begin(const StereoNativeClassifyInfo &,
                                                             uint32_t entryMxcsr) noexcept;
void stereo_audit_native_classify_end(const StereoNativeClassifyToken &, uint32_t returned,
                                      uint32_t postMxcsr) noexcept;
void stereo_audit_native_classify_hook_available(bool) noexcept;
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
StereoAuditToken stereo_audit_begin(StereoAuditPass, const void* expectedMainView=nullptr);
void stereo_audit_end(StereoAuditToken&);
int stereo_audit_group(int group);
void stereo_audit_stop();
class StereoAuditScope {
    StereoAuditToken token_;
public:
    explicit StereoAuditScope(StereoAuditPass pass, const void* expectedMainView=nullptr)
        :token_(stereo_audit_begin(pass,expectedMainView)){}
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
