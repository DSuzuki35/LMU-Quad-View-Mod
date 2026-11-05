#pragma once
#include "xr_frame_inputs.hpp"
#include "native_stereo_camera.hpp"
#include "native_stereo_transform.hpp"
#include "native_stereo_view_math.hpp"

namespace lmuqv {
struct NativeStereoFocusEye {
    XrPosef runtimePose{}; // Tracking-space pose, not an engine world position.
    Matrix current{},projection{},farProjection{},derived{};
};
struct NativeStereoFocusInputs {
    bool complete=false;
    const char* reason="incomplete focus inputs";
    StereoFrameIdentity identity;
    std::array<NativeStereoFocusEye,2> eyes{};
    std::array<Matrix,2> outerProjection{},outerFarProjection{};
    // Numeric production does not establish object/resource ownership, native
    // side effects, right-eye visibility or target/consumer compatibility.
    bool renderingEligible=false;
};
namespace native_stereo_focus_detail {
inline bool pose_valid(const XrPosef& p){
    const float values[]={p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w,
        p.position.x,p.position.y,p.position.z};
    for(const auto value:values)if(!std::isfinite(value))return false;
    const double norm=double(p.orientation.x)*p.orientation.x+double(p.orientation.y)*p.orientation.y+
        double(p.orientation.z)*p.orientation.z+double(p.orientation.w)*p.orientation.w;
    return std::abs(norm-1.0)<1e-4;
}
inline bool same_pose(const XrPosef& a,const XrPosef& b){
    if(!pose_valid(a)||!pose_valid(b))return false;
    const float dot=a.orientation.x*b.orientation.x+a.orientation.y*b.orientation.y+
        a.orientation.z*b.orientation.z+a.orientation.w*b.orientation.w;
    const float sign=dot<0?-1.f:1.f;
    return std::abs(a.position.x-b.position.x)<1e-5f&&std::abs(a.position.y-b.position.y)<1e-5f&&
        std::abs(a.position.z-b.position.z)<1e-5f&&std::abs(a.orientation.x-sign*b.orientation.x)<1e-5f&&
        std::abs(a.orientation.y-sign*b.orientation.y)<1e-5f&&std::abs(a.orientation.z-sign*b.orientation.z)<1e-5f&&
        std::abs(a.orientation.w-sign*b.orientation.w)<1e-5f;
}
}

// Runs before left focus rendering, using only owned current-frame numbers.
// Right current/derived use the existing restricted native camera model; the
// actual focus FOV now comes from the locked XR frame rather than the outer FOV.
// Never invoke native methods on these value objects.
inline NativeStereoFocusInputs prepare_native_stereo_focus_inputs(
    const QuadFrameInputs& frame,const NativeStereoCameraSnapshot& left,
    float nearZ,float farZ,float scaleX=1.f,float scaleY=1.f) noexcept {
    NativeStereoFpGuard restore;
    const auto refuse=[](const char* reason){NativeStereoFocusInputs out;out.reason=reason;return out;};
    if(!native_stereo_transform_default_controls(_mm_getcsr()))return refuse("unsupported floating-point controls");
    if(!frame.identity.valid())return refuse("missing rendered frame identity");
    if(!left.complete||left.eye!=0)return refuse("missing left outer camera");
    if(!native_camera_detail::finite(left)||!native_camera_detail::supported(left))return refuse("unsupported native camera path");
    if(left.projection[11]!=1.f||left.projection[15]!=0.f||
       left.farProjection[11]!=1.f||left.farProjection[15]!=0.f)return refuse("unsupported native perspective convention");
    if(!(nearZ>0&&nearZ<1000000.f&&farZ>nearZ)||!std::isfinite(farZ))return refuse("invalid clipping planes");
    for(const auto& view:frame.views)if(view.type!=XR_TYPE_VIEW||view.next)return refuse("unsupported XR view header");
    for(unsigned eye=0;eye<2;++eye)
        if(!native_stereo_focus_detail::same_pose(frame.views[eye].pose,frame.views[eye+2].pose))
            return refuse("outer/focus pose mismatch");
    auto leftPose=frame.views[0].pose,rightPose=frame.views[1].pose;
    leftPose.position=rightPose.position={0,0,0};
    if(!native_stereo_focus_detail::same_pose(leftPose,rightPose))return refuse("unsupported canted XR eyes");
    NativeStereoFocusInputs out;
    out.identity=frame.identity;
    out.outerProjection[0]=left.projection;out.outerFarProjection[0]=left.farProjection;
    out.outerProjection[1]=replace_fov(left.projection,left.engineProjection[1]);
    out.outerFarProjection[1]=replace_fov(left.farProjection,left.engineFarProjection[1]);
    out.eyes[0].current=left.current;
    out.eyes[1].current=multiply(multiply(left.current,left.relativeEye[1]),
        native_camera_detail::rigid_inverse(left.relativeEye[0]));
    for(unsigned eye=0;eye<2;++eye){
        const auto& view=frame.views[eye+2];
        Matrix fovProjection{};
        if(!make_engine_projection({view.fov.angleLeft,view.fov.angleRight,view.fov.angleUp,view.fov.angleDown},
            nearZ,farZ,scaleX,scaleY,fovProjection))return refuse("invalid focus projection input");
        auto& focus=out.eyes[eye];focus.runtimePose=view.pose;
        // Preserve separate native normal/far depth mappings. The four FOV
        // coefficients are independent of clipping planes; calculate once.
        focus.projection=replace_fov(out.outerProjection[eye],fovProjection);
        focus.farProjection=replace_fov(out.outerFarProjection[eye],fovProjection);
        if(!native_stereo_view_product(focus.current,focus.projection,focus.derived))
            return refuse("non-finite focus derived matrix");
        if(!native_camera_detail::finite(focus.current)||!native_camera_detail::finite(focus.derived))
            return refuse("non-finite focus camera arithmetic");
    }
    out.complete=true;out.reason="both focus cameras prepared before left focus";return out;
}

// Renderer-thread-owned input storage. Consumption can reuse exact prepared
// focus projections while both original native eye draws remain available.
// A changed frame/view/scene/depth mapping discards the pair before fallback.
class NativeStereoFocusFrame {
    NativeStereoFocusInputs inputs_;
    uint64_t scene_=0,view_=0;
    unsigned consumed_=0;
public:
    void reset(){inputs_={};scene_=view_=0;consumed_=0;}
    bool prepare(const QuadFrameInputs& frame,const NativeStereoCameraSnapshot& left,
        uint64_t scene,uint64_t view,float nearZ,float farZ,float scaleX=1.f,float scaleY=1.f){
        reset();if(!scene||!view){inputs_.reason="missing native scene/view identity";return false;}
        inputs_=prepare_native_stereo_focus_inputs(frame,left,nearZ,farZ,scaleX,scaleY);
        if(!inputs_.complete)return false;
        scene_=scene;view_=view;return true;
    }
    const NativeStereoFocusInputs* inputs(const StereoFrameIdentity& key)const {
        return inputs_.complete&&inputs_.identity==key?&inputs_:nullptr;
    }
    bool consume(unsigned eye,const StereoFrameIdentity& key,uint64_t scene,uint64_t view,
        const Matrix& nativeProjection,const Matrix& nativeFar,Matrix& focus,Matrix& focusFar){
        if(eye>1||!inputs_.complete)return false;
        const bool ordered=eye==0?consumed_==0:consumed_==1;
        const auto same=[](const Matrix& a,const Matrix& b){return !std::memcmp(a.data(),b.data(),sizeof(a));};
        if(inputs_.identity!=key||scene_!=scene||view_!=view||!ordered||
           !same(inputs_.outerProjection[eye],nativeProjection)||!same(inputs_.outerFarProjection[eye],nativeFar)){
            reset();return false;
        }
        focus=inputs_.eyes[eye].projection;focusFar=inputs_.eyes[eye].farProjection;consumed_|=1u<<eye;return true;
    }
    const char* reason()const{return inputs_.reason;}
};
}
