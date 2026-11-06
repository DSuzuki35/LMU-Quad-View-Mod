#pragma once
#include "sps_frame_cameras.hpp"
#include "sps_draw_list.hpp"
#include <unordered_map>
#include <algorithm>

namespace lmuqv {
enum class SpsConstantValue {World,View,Projection,WorldView,WorldViewProjection,PreviousWorldViewProjection,EyePosition,WorldPosition};
struct SpsConstantPatch {unsigned stage=0,slot=0,offset=0;SpsConstantValue value=SpsConstantValue::WorldViewProjection;};
enum class SpsDrawOrder {Opaque,Transparent,Ordered};
struct SpsBounds {std::array<float,3> center{},extent{};};
struct SpsSceneAsset {
    uint64_t key=0,version=0;
    StereoDrawPacket draw;
    SpsBounds bounds;
    std::vector<SpsConstantPatch> patches;
    unsigned layer=0;
    SpsDrawOrder order=SpsDrawOrder::Opaque;
    bool verticesInWorldSpace=false;
};
struct SpsSceneInstance {
    uint64_t key=0,asset=0,parameterVersion=0;
    Matrix world{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    unsigned viewMask=15,order=0;
    // Shared material/animation/light data is owned and prepared once. Only
    // explicit view patches are written separately for each visible view.
    std::array<std::shared_ptr<const StereoConstantBuffers>,2> constants;
};
struct SpsViewExtent {UINT width=0,height=0;};
struct SpsSceneStatistics {
    size_t objectsVisited=0,objectsPrepared=0,assetsPrepared=0,bindingSetsPrepared=0,viewTransforms=0;
    std::array<size_t,4> visibleDraws{};
};

// Complete unfiltered scene input, with persistent asset/object preparation.
// A single union walk builds all four independent view lists. This class has
// no native renderer, camera, material-preparation or fallback callback.
class SpsScene {
    enum class Phase {Idle,Prepared,Failed};
    struct Asset {SpsSceneAsset input;StereoFrameIdentity pinned;bool previous=false;};
    struct Instance {
        SpsSceneInstance input;
        uint64_t assetVersion=0,boundsAssetVersion=0,revision=1,historyFrame=0;
        std::array<uint64_t,4> baseRevision{};
        std::array<StereoDrawPacket,4> packets;
        Matrix previousWorld{};
        SpsBounds bounds;
        bool boundsDirty=true;
    };
    struct Visible {Instance* instance=nullptr;const StereoDrawPacket* draw=nullptr;unsigned layer=0;SpsDrawOrder order{};uint64_t asset=0;float distance=0;};
    DWORD owner_=GetCurrentThreadId();
    Phase phase_=Phase::Idle;
    std::unordered_map<uint64_t,Asset> assets_;
    std::unordered_map<uint64_t,Instance> instances_;
    std::array<std::vector<Visible>,4> visible_;
    std::array<std::array<SpsDrawList,2>,2> lists_;
    std::vector<Instance*> prepared_;
    StereoFrameIdentity identity_;
    SpsSceneStatistics statistics_;
    const char* reason_="SPS scene is empty";
    bool editable()const{return owner_==GetCurrentThreadId()&&phase_!=Phase::Prepared;}
    static unsigned patch_bytes(SpsConstantValue value){return value==SpsConstantValue::EyePosition||value==SpsConstantValue::WorldPosition?16:64;}
    static bool finite(const Matrix& m){for(float f:m)if(!std::isfinite(f))return false;return m[3]==0&&m[7]==0&&m[11]==0&&m[15]==1;}
    static bool bounds_valid(const SpsBounds& b){for(unsigned i=0;i<3;++i)if(!std::isfinite(b.center[i])||!std::isfinite(b.extent[i])||b.extent[i]<0)return false;return true;}
    static SpsBounds world_bounds(const SpsBounds& b,const Matrix& world,bool worldSpace){
        if(worldSpace)return b;SpsBounds out;
        for(unsigned axis=0;axis<3;++axis){out.center[axis]=world[12+axis];
            for(unsigned k=0;k<3;++k){out.center[axis]+=b.center[k]*world[k*4+axis];out.extent[axis]+=b.extent[k]*std::abs(world[k*4+axis]);}}
        return out;
    }
    static bool admitted(const SpsBounds& b,const SpsOwnedView& view){
        for(const auto& p:view.planes){const float distance=p.x*b.center[0]+p.y*b.center[1]+p.z*b.center[2]+p.w;
            const float radius=std::abs(p.x)*b.extent[0]+std::abs(p.y)*b.extent[1]+std::abs(p.z)*b.extent[2];
            if(distance>radius+1e-5f)return false;}
        return true;
    }
    static bool stamp(StereoDrawPacket& draw,StereoGpuResourcePool& resources){
        if(!resources.stamp(draw.indices))return false;
        for(auto& v:draw.vertices)if(!resources.stamp(v.buffer))return false;
        for(auto& stage:draw.stages)for(auto& r:stage.resources)if(r.resource&&!resources.stamp(r.resource))return false;
        return true;
    }
    bool prepare_view(Instance& object,const Asset& asset,const SpsFrameCameraData& cameras,unsigned view,const SpsViewExtent& extent,StereoGpuResourcePool& resources){
        auto& draw=object.packets[view];const auto& camera=cameras.views[view];
        for(unsigned stage=0;stage<2;++stage)draw.stages[stage].objectConstants=object.input.constants[stage];
        if(object.baseRevision[view]!=object.revision){
            // Rebase only the view-dependent images after material/pose data
            // changes. Other shared CBs are never cloned per eye or frame.
            std::array<uint32_t,2> rebased{};
            for(const auto& patch:asset.input.patches){if(rebased[patch.stage]&(1u<<patch.slot))continue;rebased[patch.stage]|=1u<<patch.slot;
                auto& stage=draw.stages[patch.stage];auto& destination=stage.constants[patch.slot];
                destination.clear();destination=stage.constant(patch.slot);}
            object.baseRevision[view]=object.revision;
        }
        if(!stamp(draw,resources))return false;
        draw.viewport={0,0,float(extent.width),float(extent.height),0,1};draw.scissor={0,0,LONG(extent.width),LONG(extent.height)};
        NativeStereoTransformInput input;input.world=object.input.world;input.view=camera.view;input.derived=camera.viewProjection;
        input.position=camera.constantPosition;input.localTransform=!asset.input.verticesInWorldSpace;
        const auto transformed=prepare_native_stereo_transform(input);if(!transformed.available)return false;++statistics_.viewTransforms;
        Matrix previous=transformed.constants.derived;const bool history=asset.previous&&cameras.historyValid&&object.historyFrame+1==cameras.identity.frame;
        if(history){const auto& world=asset.input.verticesInWorldSpace?Matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}:object.previousWorld;
            if(!stereo_view_product_values(world,camera.previousViewProjection,previous))return false;
        }
        for(const auto& patch:asset.input.patches){const void* value=nullptr;std::array<float,4> position{};
            switch(patch.value){
            case SpsConstantValue::World:value=transformed.constants.world.data();break;
            case SpsConstantValue::View:value=camera.view.data();break;
            case SpsConstantValue::Projection:value=camera.projection.data();break;
            case SpsConstantValue::WorldView:value=transformed.constants.view.data();break;
            case SpsConstantValue::WorldViewProjection:value=transformed.constants.derived.data();break;
            case SpsConstantValue::PreviousWorldViewProjection:value=previous.data();break;
            case SpsConstantValue::EyePosition:value=transformed.constants.position.data();break;
            case SpsConstantValue::WorldPosition:position={camera.worldPosition[0],camera.worldPosition[1],camera.worldPosition[2],1};value=position.data();break;
            }
            auto& image=draw.stages[patch.stage].constants[patch.slot];std::memcpy(image.data()+patch.offset,value,patch_bytes(patch.value));
        }
        return true;
    }
public:
    bool set_asset(SpsSceneAsset input){
        if(!editable()||!input.key||!input.version||!input.draw.pipeline||!bounds_valid(input.bounds))return false;
        const auto found=assets_.find(input.key);if(found!=assets_.end()&&input.version<=found->second.input.version)return false;
        for(size_t i=0;i<input.patches.size();++i){const auto& p=input.patches[i];
            if(p.stage>1||p.slot>=14||p.offset%16||unsigned(p.value)>unsigned(SpsConstantValue::WorldPosition))return false;
            const auto& image=input.draw.stages[p.stage].constant(p.slot);
            if(p.offset>image.size()||patch_bytes(p.value)>image.size()-p.offset)return false;
            for(size_t j=0;j<i;++j){const auto& a=input.patches[j];if(a.stage==p.stage&&a.slot==p.slot&&
                p.offset<a.offset+patch_bytes(a.value)&&a.offset<p.offset+patch_bytes(p.value))return false;}
        }
        try{
            if(!input.draw.bindingDevices||!input.draw.bindingDevices->matches(input.draw,input.draw.pipeline->device.get()))
                input.draw.bindingDevices=StereoBindingDevices::prepare(input.draw);
            if(!input.draw.bindingDevices){reason_="SPS material state device differs";return false;}
            for(auto& stage:input.draw.stages){auto shared=std::make_shared<StereoConstantBuffers>();
                for(unsigned slot=0;slot<14;++slot)(*shared)[slot]=stage.constant(slot);
                stage.constants={};stage.objectConstants.reset();stage.sharedConstants=std::move(shared);}
            const auto key=input.key;auto& asset=assets_[key];asset.input=std::move(input);asset.pinned={};asset.previous=false;
            for(const auto& patch:asset.input.patches)asset.previous|=patch.value==SpsConstantValue::PreviousWorldViewProjection;return true;
        }catch(...){reason_="SPS asset allocation failed";return false;}
    }
    bool set_instance(SpsSceneInstance input){
        if(!editable()||!input.key||!assets_.count(input.asset)||input.viewMask>15||!finite(input.world))return false;
        try{auto& object=instances_[input.key];
            if(std::memcmp(object.input.world.data(),input.world.data(),64))object.boundsDirty=true;
            if(object.input.asset!=input.asset){object.assetVersion=object.boundsAssetVersion=0;object.boundsDirty=true;}
            if(object.input.parameterVersion!=input.parameterVersion||object.input.constants!=input.constants)++object.revision;
            object.input=std::move(input);return true;
        }catch(...){reason_="SPS instance allocation failed";return false;}
    }
    bool erase_instance(uint64_t key){return editable()&&instances_.erase(key)!=0;}
    bool prepare(ID3D11DeviceContext* context,const SpsFrameCameraData& cameras,const std::array<SpsViewExtent,4>& extents,StereoGpuResourcePool& resources){
        if(!editable()||!cameras.identity.valid()||cameras.identity==identity_||!resources.matches(context,cameras.identity))return false;
        NativeStereoFpGuard preserve(true);const auto fail=[&](const char* why){phase_=Phase::Failed;reason_=why;return false;};
        for(const auto& extent:extents)if(!extent.width||!extent.height||extent.width>16384||extent.height>16384)return fail("invalid SPS scene view extent");
        statistics_={};prepared_.clear();for(auto& view:visible_)view.clear();for(auto& pair:lists_)for(auto& view:pair)view.clear();
        try{
            for(auto& entry:instances_){auto& object=entry.second;++statistics_.objectsVisited;if(!object.input.viewMask)continue;
                const auto at=assets_.find(object.input.asset);if(at==assets_.end())return fail("SPS instance asset missing");auto& asset=at->second;
                if(object.boundsAssetVersion!=asset.input.version)object.boundsDirty=true;
                if(object.boundsDirty){object.bounds=world_bounds(asset.input.bounds,object.input.world,asset.input.verticesInWorldSpace);
                    if(!bounds_valid(object.bounds))return fail("SPS world bounds overflow");object.boundsDirty=false;object.boundsAssetVersion=asset.input.version;}
                bool prepared=false;
                for(unsigned view=0;view<4;++view)if((object.input.viewMask&(1u<<view))&&admitted(object.bounds,cameras.views[view])){
                    if(object.assetVersion!=asset.input.version){for(auto& packet:object.packets){packet=asset.input.draw;packet.key=object.input.key;}
                        object.assetVersion=asset.input.version;++object.revision;++statistics_.bindingSetsPrepared;}
                    if(asset.pinned!=cameras.identity){if(!stamp(asset.input.draw,resources))return fail("SPS asset snapshot unavailable");
                        asset.pinned=cameras.identity;++statistics_.assetsPrepared;}
                    if(!prepare_view(object,asset,cameras,view,extents[view],resources))return fail("SPS owned draw preparation failed");
                    const auto& p=cameras.views[view].worldPosition;float distance=0;
                    for(unsigned axis=0;axis<3;++axis){const float delta=object.bounds.center[axis]-p[axis];distance+=delta*delta;}
                    if(!std::isfinite(distance))return fail("SPS sort distance overflow");
                    visible_[view].push_back({&object,&object.packets[view],asset.input.layer,asset.input.order,asset.input.key,distance});prepared=true;
                }
                if(prepared){++statistics_.objectsPrepared;prepared_.push_back(&object);}
            }
            for(unsigned view=0;view<4;++view){auto& visible=visible_[view];std::sort(visible.begin(),visible.end(),[](const Visible& a,const Visible& b){
                    if(a.layer!=b.layer)return a.layer<b.layer;
                    if(a.order!=b.order)return unsigned(a.order)<unsigned(b.order);
                    if(a.order==SpsDrawOrder::Transparent&&a.distance!=b.distance)return a.distance>b.distance;
                    if(a.order==SpsDrawOrder::Ordered&&a.instance->input.order!=b.instance->input.order)return a.instance->input.order<b.instance->input.order;
                    if(a.order==SpsDrawOrder::Opaque&&a.asset!=b.asset)return a.asset<b.asset;
                    return a.draw->key<b.draw->key;
                });
                auto& list=lists_[view/2][view%2];list.reserve(visible.size());for(const auto& item:visible)list.push_back(*item.draw);
                statistics_.visibleDraws[view]=visible.size();
            }
            identity_=cameras.identity;phase_=Phase::Prepared;reason_="complete owned SPS quad scene workload";return true;
        }catch(...){return fail("SPS scene frame allocation failed");}
    }
    const std::array<SpsDrawList,2>* pair(const StereoFrameIdentity& key,unsigned pair)const{
        return owner_==GetCurrentThreadId()&&phase_==Phase::Prepared&&key==identity_&&pair<2?&lists_[pair]:nullptr;
    }
    bool finish(const StereoFrameIdentity& key){
        if(!pair(key,0))return false;for(auto* object:prepared_){object->previousWorld=object->input.world;object->historyFrame=key.frame;}phase_=Phase::Idle;return true;
    }
    void abort(){if(owner_==GetCurrentThreadId()){phase_=Phase::Failed;reason_="SPS scene frame aborted";}}
    const SpsSceneStatistics& statistics()const{return statistics_;}
    const char* reason()const{return reason_;}
};
}
