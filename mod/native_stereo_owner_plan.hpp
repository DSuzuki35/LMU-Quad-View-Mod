#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>
#include "native_stereo_owner_bounds.hpp"

namespace lmuqv {
inline constexpr std::size_t nativeStereoOwnerNodeLimit = 4096;
inline constexpr std::size_t nativeStereoOwnerSourceLimit = 65536;

// Owned scalar state only. Values are opaque tokens, never native addresses.
struct NativeStereoOwnerState {
    std::uint32_t flags{}, mask{};
    std::uint64_t pendingValue{}, currentValue{};
    bool attachmentPresent{};
};
struct NativeStereoOwner {
    NativeStereoOwnerState state{};
    std::vector<std::uint32_t> children; // Ordered indices into input.owners.
    std::vector<std::uint32_t> sourceIds; // Opaque IDs, not owned draw inputs.
    NativeStereoOwnerBounds bounds;
};
struct NativeStereoOwnerView {
    std::uint32_t mask{0xffffffffu};
    bool parentNull{true};
    NativeStereoOwnerBoundsView bounds;
};
struct NativeStereoOwnerPlanInput {
    std::vector<NativeStereoOwner> owners;
    std::uint32_t root{}, globalMask{0xffffffffu};
    std::array<NativeStereoOwnerView, 2> views{}; // Left, then right.
    // Optional ordered forest. Empty retains the single-root interface.
    // These are actual roots; no fabricated owner/callback joins the trees.
    std::vector<std::uint32_t> roots;
};
enum class NativeStereoOwnerEye { Left, Right };
enum class NativeStereoOwnerField { CurrentValue, Flags };
enum class NativeStereoOwnerPlanReason {
    None, NodeLimit, SourceLimit, InvalidRoot, InvalidChildIndex, InvalidTree,
    DuplicateSourceId, UnsupportedOwnerFlags, AllocationFailure, UnsupportedOwnerBounds
};
struct NativeStereoOwnerTransition {
    NativeStereoOwnerEye eye{};
    std::uint32_t ownerIndex{};
    NativeStereoOwnerField field{};
    std::uint64_t before{}, after{};
};
struct NativeStereoOwnerEyePlan {
    std::vector<std::uint32_t> ownerOrder;
    // State seen by source emission, before this owner's children/final clear.
    std::vector<NativeStereoOwnerState> emissionStates;
    std::vector<std::uint32_t> sourceOrder; // Unfiltered, owned source IDs in DFS order.
};
struct NativeStereoOwnerPlanResult {
    bool complete{};
    // Success establishes owner admission only, never drawable eligibility.
    bool renderingEligible{false};
    NativeStereoOwnerPlanReason reason{NativeStereoOwnerPlanReason::None};
    std::vector<NativeStereoOwnerState> finalStates;
    std::array<NativeStereoOwnerEyePlan, 2> eyes{};
    std::vector<NativeStereoOwnerTransition> transitions;
};

// Models an immutable owned tree with no intervening candidate/consumer mutation.
// This contract is not established for LMU integration. No native access or writes
// occur. Bounds/structure/domain are validated before creating any output payload.
// A complete left DFS precedes right DFS on shared copied state; no distance sort.
// Refusal (including allocation failure) returns empty payloads and never mutates
// input. These bounds define this planner's domain, not engine limits.
inline NativeStereoOwnerPlanResult build_native_stereo_owner_plan(
    const NativeStereoOwnerPlanInput& in) noexcept {
    using Reason = NativeStereoOwnerPlanReason;
    const auto refuse = [](Reason reason) noexcept {
        NativeStereoOwnerPlanResult out;
        out.reason = reason;
        return out;
    };
    const auto count = in.owners.size();
    if (count == 0 || count > nativeStereoOwnerNodeLimit) return refuse(Reason::NodeLimit);
    const auto rootCount=in.roots.empty()?size_t(1):in.roots.size();
    if (rootCount>count||(in.roots.empty()&&in.root>=count)) return refuse(Reason::InvalidRoot);
    std::size_t sourceCount = 0, edgeCount = 0;
    for (const auto& owner : in.owners) {
        if (owner.sourceIds.size() > nativeStereoOwnerSourceLimit - sourceCount)
            return refuse(Reason::SourceLimit);
        sourceCount += owner.sourceIds.size();
        // Valid forests have exactly count-rootCount edges. Refuse excess before scanning
        // any potentially enormous child vector or allocating scratch storage.
        if (owner.children.size() > count - rootCount - edgeCount) return refuse(Reason::InvalidTree);
        edgeCount += owner.children.size();
    }
    try {
        // Scratch is bounded by the validated node/edge/source totals. Parent
        // counts reject both duplicate references and shared children.
        std::vector<unsigned char> parents(count, 0);
        const auto roots=in.roots.empty()?std::vector<std::uint32_t>{in.root}:in.roots;
        std::vector<unsigned char> isRoot(count,0);
        for(auto root:roots){if(root>=count||isRoot[root])return refuse(Reason::InvalidRoot);isRoot[root]=1;}
        for (const auto& owner : in.owners) {
            for (const auto child : owner.children) {
                if (child >= count) return refuse(Reason::InvalidChildIndex);
                if (++parents[child] != 1 || isRoot[child]) return refuse(Reason::InvalidTree);
            }
        }
        if (edgeCount != count - rootCount) return refuse(Reason::InvalidTree);
        for (std::size_t i = 0; i < count; ++i)
            if (!isRoot[i] && parents[i] != 1) return refuse(Reason::InvalidTree);
        std::vector<std::uint32_t> pending;
        pending.reserve(count);
        for(auto root:roots)pending.push_back(root);
        std::size_t reached = 0;
        while (!pending.empty()) {
            const auto index = pending.back();
            pending.pop_back();
            ++reached;
            // Unique parents and no root-parent edge mean the root's component
            // is acyclic. Any disconnected cycle is caught by reached != count.
            for (const auto child : in.owners[index].children) pending.push_back(child);
        }
        if (reached != count) return refuse(Reason::InvalidTree);
        std::vector<std::uint32_t> sourceIds;
        sourceIds.reserve(sourceCount);
        for (const auto& owner : in.owners) {
            // Conservative whole-tree refusal, even behind rejected parents.
            if (!owner.state.attachmentPresent && (owner.state.flags & 0x400000u))
                return refuse(Reason::UnsupportedOwnerFlags);
            if (!owner.state.attachmentPresent && (owner.state.flags & 0x80000u))
                for(const auto& view:in.views)
                    if(!classify_native_stereo_owner_bounds(owner.bounds,view.bounds,owner.state.flags).available)
                        return refuse(Reason::UnsupportedOwnerBounds);
            sourceIds.insert(sourceIds.end(), owner.sourceIds.begin(), owner.sourceIds.end());
        }
        std::sort(sourceIds.begin(), sourceIds.end());
        if (std::adjacent_find(sourceIds.begin(), sourceIds.end()) != sourceIds.end())
            return refuse(Reason::DuplicateSourceId);

        NativeStereoOwnerPlanResult out;
        out.finalStates.reserve(count);
        for (const auto& owner : in.owners) out.finalStates.push_back(owner.state);
        for (auto& eye : out.eyes) {
            eye.ownerOrder.reserve(count);
            eye.emissionStates.reserve(count);
            eye.sourceOrder.reserve(sourceCount);
        }
        // Each eye can assign current, execute three bounds writes and clear flags.
        out.transitions.reserve(10 * count);
        struct Frame { std::uint32_t owner; std::size_t nextChild; };
        std::vector<Frame> stack;
        stack.reserve(count);
        for (std::size_t eyeIndex = 0; eyeIndex < 2; ++eyeIndex) {
            const auto eye = eyeIndex == 0 ? NativeStereoOwnerEye::Left : NativeStereoOwnerEye::Right;
            const auto write = [&](std::uint32_t index, NativeStereoOwnerField field,
                                   std::uint64_t before, std::uint64_t after) {
                if (before != after) out.transitions.push_back({eye, index, field, before, after});
            };
            const auto finishOwner=[&](std::uint32_t index){
                if(in.views[eyeIndex].parentNull){auto& state=out.finalStates[index];
                    const auto flags=state.flags&~0x40000000u;
                    write(index,NativeStereoOwnerField::Flags,state.flags,flags);state.flags=flags;}
            };
            bool boundsFailed=false;
            const auto enter = [&](std::uint32_t index) {
                auto& state = out.finalStates[index];
                if (!state.attachmentPresent) {
                    write(index, NativeStereoOwnerField::CurrentValue, state.currentValue, state.pendingValue);
                    state.currentValue = state.pendingValue;
                }
                // These gates are unconditional, including nonnull attachments.
                if ((state.flags & 0x20000u) || (state.mask & in.globalMask) != state.mask ||
                    (state.mask & in.views[eyeIndex].mask) != state.mask) return false;
                if (!state.attachmentPresent) {
                    if(state.flags&0x80000u){
                        const auto bounds=classify_native_stereo_owner_bounds(in.owners[index].bounds,in.views[eyeIndex].bounds,state.flags);
                        if(!bounds.available){boundsFailed=true;return false;}
                        for(unsigned i=0;i<bounds.writeCount;++i){
                            write(index,NativeStereoOwnerField::Flags,state.flags,bounds.flagWrites[i]);state.flags=bounds.flagWrites[i];}
                        if(!bounds.admitted){finishOwner(index);return false;}
                    }else{
                        const auto flags = state.flags | 0x10000u;
                        write(index, NativeStereoOwnerField::Flags, state.flags, flags);
                        state.flags = flags;
                    }
                }
                auto& plan = out.eyes[eyeIndex];
                plan.ownerOrder.push_back(index);
                plan.emissionStates.push_back(state);
                const auto& ids = in.owners[index].sourceIds;
                plan.sourceOrder.insert(plan.sourceOrder.end(), ids.begin(), ids.end());
                return true;
            };
            for(auto root:roots){
              if (enter(root)) stack.push_back({root, 0});
              while (!stack.empty()) {
                auto& frame = stack.back();
                const auto& children = in.owners[frame.owner].children;
                if (frame.nextChild < children.size()) {
                    const auto child = children[frame.nextChild++];
                    if (enter(child)) stack.push_back({child, 0});
                } else {
                    finishOwner(frame.owner);
                    stack.pop_back();
                }
              }
            }
            if(boundsFailed)return refuse(Reason::UnsupportedOwnerBounds);
        }
        out.complete = true;
        return out;
    } catch (const std::bad_alloc&) {
        return refuse(Reason::AllocationFailure);
    }
}
} // namespace lmuqv
