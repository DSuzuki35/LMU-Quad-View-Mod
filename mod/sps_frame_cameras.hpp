#pragma once
#include <windows.h>
#include "xr_frame_inputs.hpp"
#include "native_stereo_view_math.hpp"
#include <array>
#include <cmath>

namespace lmuqv {
struct SpsCameraPlacement {
    // Engine-world placement of the OpenXR tracking origin, using row vectors.
    // This is shared camera/vehicle data, not an independently rendered eye.
    Matrix worldFromTracking{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    float worldUnitsPerMeter=1,nearClip=.05f,farClip=10000;
    uint64_t camera=1;
    bool reversedDepth=true,cameraCut=false;
};
struct SpsOwnedView {
    Matrix worldFromEye{},view{},projection{},viewProjection{},previousViewProjection{};
    std::array<float,3> worldPosition{},constantPosition{};
    std::array<NativeStereoClassifyPlane,6> planes{};
    XrPosef runtimePose{};
    XrFovf fov{};
};
struct SpsFrameCameraData {
    StereoFrameIdentity identity;
    uint64_t camera=0;
    float nearClip=0,farClip=0;
    bool reversedDepth=false,historyValid=false;
    // Runtime quad order: outer left/right, focus left/right.
    std::array<SpsOwnedView,4> views;
};
namespace sps_camera_detail {
inline bool rigid(const Matrix& m){
    for(float v:m)if(!std::isfinite(v))return false;
    if(m[3]!=0||m[7]!=0||m[11]!=0||m[15]!=1)return false;
    for(unsigned a=0;a<3;++a)for(unsigned b=a;b<3;++b){
        float dot=0;for(unsigned k=0;k<3;++k)dot+=m[a*4+k]*m[b*4+k];
        if(std::abs(dot-(a==b?1.f:0.f))>1e-4f)return false;
    }
    const float determinant=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
    return determinant>0;
}
inline Matrix inverse(const Matrix& m){
    Matrix out{m[0],m[4],m[8],0,m[1],m[5],m[9],0,m[2],m[6],m[10],0,0,0,0,1};
    for(unsigned col=0;col<3;++col)out[12+col]=-((m[12]*out[col]+m[13]*out[4+col])+m[14]*out[8+col]);
    return out;
}
inline bool eye_matrix(const XrPosef& pose,float units,Matrix& out){
    const float all[]{pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w,pose.position.x,pose.position.y,pose.position.z};
    for(float v:all)if(!std::isfinite(v))return false;
    // OpenXR looks along -Z. The engine projection uses +Z; conjugating by
    // the Z reflection converts both pose orientation and translation once.
    const float x=-pose.orientation.x,y=-pose.orientation.y,z=pose.orientation.z,w=pose.orientation.w;
    const float norm=x*x+y*y+z*z+w*w;if(std::abs(norm-1.f)>1e-4f)return false;
    out={1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
         2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
         2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,
         pose.position.x*units,pose.position.y*units,-pose.position.z*units,1};
    return rigid(out);
}
}

// Own the four views once. No native camera setter, rendered-left-eye matrix,
// later-eye prediction/comparison or previous-frame current pose is required.
class SpsFrameCameras {
    enum class Phase {Empty,Prepared,Complete,Failed};
    Phase phase_=Phase::Empty;
    SpsFrameCameraData current_,history_;
    const char* reason_="SPS cameras not prepared";
public:
    bool prepare(const QuadFrameInputs& frame,const SpsCameraPlacement& placement){
        NativeStereoFpGuard preserve(true);
        if(current_.identity.valid()&&current_.identity.thread!=GetCurrentThreadId())return false;
        const auto fail=[&](const char* why){reason_=why;phase_=Phase::Failed;return false;};
        if(phase_==Phase::Prepared)return false;
        if(!frame.identity.valid()||frame.identity.thread!=GetCurrentThreadId()||frame.identity==current_.identity)
            return fail("invalid or already-owned SPS camera frame");
        if(!placement.camera||!sps_camera_detail::rigid(placement.worldFromTracking)||
           !(placement.worldUnitsPerMeter>0)||!std::isfinite(placement.worldUnitsPerMeter)||
           !(placement.nearClip>0&&placement.farClip>placement.nearClip)||!std::isfinite(placement.farClip))
            return fail("invalid SPS shared camera placement");
        SpsFrameCameraData out;out.identity=frame.identity;out.camera=placement.camera;
        out.nearClip=placement.nearClip;out.farClip=placement.farClip;out.reversedDepth=placement.reversedDepth;
        out.historyValid=!placement.cameraCut&&history_.camera==placement.camera&&history_.identity.valid()&&
            history_.identity.session==frame.identity.session&&history_.identity.space==frame.identity.space&&
            history_.identity.frame+1==frame.identity.frame&&history_.identity.displayTime<frame.identity.displayTime;
        for(unsigned i=0;i<4;++i){
            const auto& input=frame.views[i];auto& eye=out.views[i];Matrix trackingFromEye;
            if(input.type!=XR_TYPE_VIEW||input.next||!sps_camera_detail::eye_matrix(input.pose,placement.worldUnitsPerMeter,trackingFromEye))
                return fail("invalid owned XR eye input");
            if(!native_stereo_view_product(trackingFromEye,placement.worldFromTracking,eye.worldFromEye)||!sps_camera_detail::rigid(eye.worldFromEye))
                return fail("SPS world camera transform unavailable");
            eye.view=sps_camera_detail::inverse(eye.worldFromEye);eye.runtimePose=input.pose;eye.fov=input.fov;
            for(unsigned axis=0;axis<3;++axis){eye.worldPosition[axis]=eye.worldFromEye[12+axis];eye.constantPosition[axis]=-eye.worldPosition[axis];}
            if(!make_engine_projection({input.fov.angleLeft,input.fov.angleRight,input.fov.angleUp,input.fov.angleDown},
                placement.nearClip,placement.farClip,1,1,eye.projection))return fail("invalid SPS eye projection");
            if(placement.reversedDepth){const float inverse=1.f/(placement.farClip-placement.nearClip);
                eye.projection[10]=-placement.nearClip*inverse;eye.projection[14]=placement.nearClip*placement.farClip*inverse;}
            const auto geometry=prepare_native_stereo_view_geometry(eye.view,eye.projection);
            if(!geometry.complete)return fail(geometry.reason);eye.viewProjection=geometry.derived;eye.planes=geometry.planes;
            eye.previousViewProjection=out.historyValid?history_.views[i].viewProjection:eye.viewProjection;
        }
        current_=std::move(out);phase_=Phase::Prepared;reason_="one authoritative SPS quad camera frame";return true;
    }
    const SpsFrameCameraData* get(const StereoFrameIdentity& key)const{
        return phase_==Phase::Prepared&&key==current_.identity&&key.thread==GetCurrentThreadId()?&current_:nullptr;
    }
    bool finish(const StereoFrameIdentity& key){
        if(!get(key))return false;history_=current_;phase_=Phase::Complete;return true;
    }
    void abort(){if(!current_.identity.valid()||current_.identity.thread==GetCurrentThreadId()){phase_=Phase::Failed;reason_="SPS camera frame aborted";}}
    const char* reason()const{return reason_;}
};
}
