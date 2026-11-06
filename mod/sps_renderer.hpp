#pragma once
#include "sps_scene.hpp"
#include "sps_frame_targets.hpp"
#include "stereo_batch.hpp"

namespace lmuqv {
struct SpsRendererDescription {
    std::array<SpsViewExtent,4> views;
    DXGI_FORMAT color=DXGI_FORMAT_R16G16B16A16_FLOAT,depth=DXGI_FORMAT_D32_FLOAT;
    UINT samples=1,quality=0;
    std::array<float,4> background{0,0,0,1};
};
struct SpsFrameStatistics {
    SpsSceneStatistics scene;
    std::array<StereoBatchStatistics,2> batches;
    uint64_t resourceCopyBytes=0,resourceSnapshots=0;
};

// Full SPS-owned quad rendering. Both complete workloads are preflighted before
// submission and images are published only when the whole quad frame is ready.
// There is intentionally no original-render/native-fallback entry point.
class SpsRenderer {
    enum class Phase {Empty,Rendered,Complete,Failed};
    Phase phase_=Phase::Empty;
    SpsFrameCameras cameras_;
    StereoGpuResourcePool resources_;
    std::array<SpsFrameTargets,2> targets_;
    std::array<StereoBatch,2> batches_;
    StereoFrameIdentity identity_;
    SpsScene* scene_=nullptr;
    SpsFrameStatistics statistics_;
    const char* reason_="SPS renderer not prepared";
    bool fail(const char* why){
        reason_=why;for(auto& batch:batches_)batch.abort();for(auto& target:targets_)target.abort();
        cameras_.abort();if(scene_)scene_->abort();scene_=nullptr;phase_=Phase::Failed;return false;
    }
public:
    bool render(ID3D11DeviceContext* context,const QuadFrameInputs& frame,const SpsCameraPlacement& placement,
        const SpsRendererDescription& description,SpsScene& scene){
        if(phase_==Phase::Rendered||(!identity_.valid()?false:identity_.thread!=GetCurrentThreadId()))return false;
        scene_=&scene;statistics_={};
        if(!cameras_.prepare(frame,placement))return fail(cameras_.reason());
        identity_=frame.identity;if(!resources_.begin(context,identity_))return fail(resources_.reason());
        const auto* cameras=cameras_.get(identity_);if(!cameras)return fail("SPS camera ownership unavailable");
        for(unsigned pair=0;pair<2;++pair){
            const auto& left=description.views[pair*2];const auto& right=description.views[pair*2+1];
            SpsFrameTargetDescription target{(std::max)(left.width,right.width),(std::max)(left.height,right.height),
                description.color,description.depth,description.samples,description.quality};
            SpsFrameClear clear;clear.color=description.background;clear.depth=placement.reversedDepth?0.f:1.f;
            if(!targets_[pair].begin(context,identity_,target,clear))return fail(targets_[pair].reason());
        }
        if(!scene.prepare(context,*cameras,description.views,resources_))return fail(scene.reason());
        for(unsigned pair=0;pair<2;++pair){const auto* draws=scene.pair(identity_,pair);
            if(!draws||!batches_[pair].prepare(context,identity_,targets_[pair],*draws))return fail(batches_[pair].reason());}
        for(unsigned pair=0;pair<2;++pair)if(!batches_[pair].execute(context,identity_,targets_[pair]))return fail(batches_[pair].reason());
        statistics_.scene=scene.statistics();for(unsigned pair=0;pair<2;++pair)statistics_.batches[pair]=batches_[pair].statistics();
        statistics_.resourceCopyBytes=resources_.copied_bytes();statistics_.resourceSnapshots=resources_.snapshots_created();
        phase_=Phase::Rendered;reason_="complete SPS quad frame rendered";return true;
    }
    bool image(unsigned view,const StereoFrameIdentity& key,SpsFrameImage& image)const{
        image={};return phase_==Phase::Rendered&&view<4&&key==identity_&&targets_[view/2].image(view%2,key,image);
    }
    bool consumed(unsigned view,const StereoFrameIdentity& key){
        return phase_==Phase::Rendered&&view<4&&key==identity_&&targets_[view/2].consumed(view%2,key);
    }
    bool finish(const StereoFrameIdentity& key){
        if(phase_!=Phase::Rendered||key!=identity_||key.thread!=GetCurrentThreadId())return false;
        // finish only after every output has been consumed; image() would fail
        // for an individual consumed view, so use each target's completion state.
        if(!targets_[0].complete(key)||!targets_[1].complete(key))return false;
        if(!scene_||!scene_->finish(key)||!cameras_.finish(key))return fail("SPS frame completion order invalid");
        scene_=nullptr;phase_=Phase::Complete;return true;
    }
    void abort(){if(!identity_.valid()||identity_.thread==GetCurrentThreadId())fail("SPS frame aborted");}
    const SpsFrameStatistics& statistics()const{return statistics_;}
    const char* reason()const{return reason_;}
};
}
