#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace lmuqv {
inline constexpr size_t stereoBatchDrawLimit=32768;
inline constexpr uint32_t stereoBatchNoDraw=(std::numeric_limits<uint32_t>::max)();
struct StereoBatchDraw {
    uint64_t key=0,pairClass=0;
};
enum class StereoBatchActionKind {Left,Right,Pair};
struct StereoBatchAction {
    StereoBatchActionKind kind{};
    uint32_t left=stereoBatchNoDraw,right=stereoBatchNoDraw;
};
struct StereoBatchPlan {
    bool complete=false;
    const char* reason="incomplete stereo order";
    size_t paired=0;
    std::vector<StereoBatchAction> actions;
};
// Pair classes are compatibility hints, never proof that pipelines/resources
// match. The executor must compare the actual frozen inputs before submission.
// This merge preserves each eye's exact draw order, including conflicting order
// of common objects. Ineligible/crossing draws become separate mono actions.
inline StereoBatchPlan plan_stereo_batch(const std::vector<StereoBatchDraw>& left,
    const std::vector<StereoBatchDraw>& right) noexcept {
    const auto refuse=[](const char* reason){StereoBatchPlan out;out.reason=reason;return out;};
    if(left.size()>stereoBatchDrawLimit||right.size()>stereoBatchDrawLimit)return refuse("stereo draw limit");
    try {
        std::unordered_map<uint64_t,uint32_t> rightIndex,leftIndex;
        for(uint32_t i=0;i<right.size();++i)if(!rightIndex.emplace(right[i].key,i).second)return refuse("duplicate right draw key");
        struct Match {uint32_t left,right;};std::vector<Match> matches;matches.reserve((std::min)(left.size(),right.size()));
        for(uint32_t i=0;i<left.size();++i){
            if(!leftIndex.emplace(left[i].key,i).second)return refuse("duplicate left draw key");
            const auto found=rightIndex.find(left[i].key);
            if(found!=rightIndex.end()&&left[i].pairClass&&left[i].pairClass==right[found->second].pairClass)
                matches.push_back({i,found->second});
        }
        // Longest increasing subsequence of right positions in left order.
        std::vector<uint32_t> tails,previous(matches.size(),stereoBatchNoDraw);tails.reserve(matches.size());
        for(uint32_t i=0;i<matches.size();++i){
            const auto at=std::lower_bound(tails.begin(),tails.end(),matches[i].right,
                [&](uint32_t index,uint32_t value){return matches[index].right<value;});
            const auto position=size_t(at-tails.begin());if(position)previous[i]=tails[position-1];
            if(at==tails.end())tails.push_back(i);else *at=i;
        }
        std::vector<Match> anchors;anchors.reserve(tails.size());
        if(!tails.empty())for(uint32_t at=tails.back();at!=stereoBatchNoDraw;at=previous[at])anchors.push_back(matches[at]);
        std::reverse(anchors.begin(),anchors.end());
        StereoBatchPlan out;out.actions.reserve(left.size()+right.size()-anchors.size());
        uint32_t l=0,r=0;
        for(const auto anchor:anchors){
            while(l<anchor.left)out.actions.push_back({StereoBatchActionKind::Left,l++,stereoBatchNoDraw});
            while(r<anchor.right)out.actions.push_back({StereoBatchActionKind::Right,stereoBatchNoDraw,r++});
            out.actions.push_back({StereoBatchActionKind::Pair,l++,r++});
        }
        while(l<left.size())out.actions.push_back({StereoBatchActionKind::Left,l++,stereoBatchNoDraw});
        while(r<right.size())out.actions.push_back({StereoBatchActionKind::Right,stereoBatchNoDraw,r++});
        out.paired=anchors.size();out.complete=true;out.reason="ordered stereo merge";return out;
    }catch(...){return refuse("stereo order allocation failure");}
}
}
