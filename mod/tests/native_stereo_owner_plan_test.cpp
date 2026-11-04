#include <cassert>
#include <iostream>
#if __has_include("../native_stereo_owner_plan.hpp")
#include "../native_stereo_owner_plan.hpp"
#include <cstdlib>
#include <new>
#include <limits>
using namespace lmuqv;
namespace {
// Test-only allocation interception: production has no failure injection API.
std::size_t allocationCount = 0, failAllocation = 0;
bool countAllocations = false;
}
void* operator new(std::size_t bytes) {
    if (countAllocations && ++allocationCount == failAllocation) throw std::bad_alloc();
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
namespace {
using Reason = NativeStereoOwnerPlanReason;
using Eye = NativeStereoOwnerEye;
using Field = NativeStereoOwnerField;
NativeStereoOwnerPlanInput tree(std::size_t count = 1) {
    NativeStereoOwnerPlanInput in;
    in.owners.resize(count);
    return in;
}
void empty_refusal(const NativeStereoOwnerPlanResult& out, Reason reason) {
    assert(!out.complete && !out.renderingEligible && out.reason == reason);
    assert(out.finalStates.empty() && out.transitions.empty());
    for (const auto& eye : out.eyes) assert(eye.ownerOrder.empty() && eye.sourceOrder.empty());
}
void success(const NativeStereoOwnerPlanResult& out) {
    assert(out.complete && out.reason == Reason::None && !out.renderingEligible);
}
void unchanged(const NativeStereoOwnerPlanInput& a, const NativeStereoOwnerPlanInput& b) {
    assert(a.root == b.root && a.globalMask == b.globalMask && a.owners.size() == b.owners.size());
    for (std::size_t i = 0; i < 2; ++i)
        assert(a.views[i].mask == b.views[i].mask && a.views[i].parentNull == b.views[i].parentNull);
    for (std::size_t i = 0; i < a.owners.size(); ++i) {
        const auto& x = a.owners[i]; const auto& y = b.owners[i];
        assert(x.children == y.children && x.sourceIds == y.sourceIds);
        assert(x.state.flags == y.state.flags && x.state.mask == y.state.mask &&
               x.state.currentValue == y.state.currentValue && x.state.pendingValue == y.state.pendingValue &&
               x.state.attachmentPresent == y.state.attachmentPresent);
    }
}
void transition(const NativeStereoOwnerPlanResult& out, std::size_t i, Eye eye,
                std::uint32_t owner, Field field, std::uint64_t before, std::uint64_t after) {
    assert(i < out.transitions.size());
    const auto& t = out.transitions[i];
    assert(t.eye == eye && t.ownerIndex == owner && t.field == field && t.before == before && t.after == after);
}
void right_only_and_order() {
    // A left-rejected root must still expose its complete right-only subtree.
    auto in = tree(4); in.owners[0].children = {2, 1}; in.owners[2].children = {3};
    in.owners[0].state.mask = 2; in.views[0].mask = 1; in.views[1].mask = 3;
    in.owners[0].sourceIds = {80, 2}; in.owners[1].sourceIds = {44};
    in.owners[2].sourceIds = {0}; in.owners[3].sourceIds = {0xffffffffu};
    const auto original = in;
    auto out = build_native_stereo_owner_plan(in); success(out);
    assert(out.eyes[0].ownerOrder.empty() && out.eyes[0].sourceOrder.empty());
    assert((out.eyes[1].ownerOrder == std::vector<std::uint32_t>{0, 2, 3, 1}));
    assert((out.eyes[1].sourceOrder == std::vector<std::uint32_t>{80, 2, 0, 0xffffffffu, 44}));
    unchanged(in, original);
    // Returned IDs/states survive input changes and destruction.
    in.owners.clear(); assert(out.eyes[1].sourceOrder.back() == 44 && out.finalStates.size() == 4);

    in = tree(3); in.owners[0].children = {1, 2}; in.owners[1].state.mask = 2;
    in.views[0].mask = 1; in.views[1].mask = 3;
    out = build_native_stereo_owner_plan(in); success(out);
    assert((out.eyes[0].ownerOrder == std::vector<std::uint32_t>{0, 2}));
    assert((out.eyes[1].ownerOrder == std::vector<std::uint32_t>{0, 1, 2}));
    // A nonzero root index is valid when all owners remain reachable.
    in = tree(2); in.root = 1; in.owners[1].children = {0};
    out = build_native_stereo_owner_plan(in); success(out);
    assert((out.eyes[0].ownerOrder == std::vector<std::uint32_t>{1, 0}));
}
void all_bits_masks() {
    // Changing inclusion to any-bit overlap admits the partial-mask cases.
    struct Case { std::uint32_t owner, global, view; bool admitted; };
    for (const auto c : {Case{0, 0, 0, true}, Case{3, 1, 3, false}, Case{3, 3, 1, false},
                         Case{3, 3, 3, true}, Case{0x80000000u, 0xffffffffu, 0x80000000u, true},
                         Case{0x80000001u, 0x80000000u, 0xffffffffu, false},
                         Case{0x80000000u, 0x7fffffffu, 0xffffffffu, false}}) {
        auto in = tree(); in.owners[0].state.mask = c.owner; in.globalMask = c.global;
        in.views[0].mask = in.views[1].mask = c.view;
        const auto out = build_native_stereo_owner_plan(in); success(out);
        assert(out.eyes[0].ownerOrder.size() == (c.admitted ? 1u : 0u));
        assert(out.eyes[1].ownerOrder.size() == (c.admitted ? 1u : 0u));
    }
}
void shared_mutations_and_postorder() {
    auto in = tree(3); in.owners[0].children = {2, 1};
    for (auto& owner : in.owners) {
        owner.state.flags = 0xc0000004u;
        owner.state.currentValue = 7; owner.state.pendingValue = 0xffffffffffffffffull;
    }
    in.views[0].parentNull = false; in.views[1].parentNull = true;
    const auto before = in;
    const auto out = build_native_stereo_owner_plan(in); success(out);
    // Left performs each assignment/set. Right observes those writes, emits only
    // child clears followed by parent clear (no duplicate assignment/set).
    assert(out.transitions.size() == 9);
    transition(out, 0, Eye::Left, 0, Field::CurrentValue, 7, 0xffffffffffffffffull);
    transition(out, 1, Eye::Left, 0, Field::Flags, 0xc0000004u, 0xc0010004u);
    transition(out, 2, Eye::Left, 2, Field::CurrentValue, 7, 0xffffffffffffffffull);
    transition(out, 3, Eye::Left, 2, Field::Flags, 0xc0000004u, 0xc0010004u);
    transition(out, 4, Eye::Left, 1, Field::CurrentValue, 7, 0xffffffffffffffffull);
    transition(out, 5, Eye::Left, 1, Field::Flags, 0xc0000004u, 0xc0010004u);
    transition(out, 6, Eye::Right, 2, Field::Flags, 0xc0010004u, 0x80010004u);
    transition(out, 7, Eye::Right, 1, Field::Flags, 0xc0010004u, 0x80010004u);
    transition(out, 8, Eye::Right, 0, Field::Flags, 0xc0010004u, 0x80010004u);
    for (const auto& s : out.finalStates)
        assert(s.flags == 0x80010004u && s.currentValue == 0xffffffffffffffffull);
    unchanged(in, before);

    // Left post-child clear is also visible to right: its before value must
    // never reappear in the second traversal from an independently copied eye.
    in.views[0].parentNull = true;
    const auto leftClear = build_native_stereo_owner_plan(in); success(leftClear);
    assert(leftClear.transitions.size() == 9);
    transition(leftClear, 4, Eye::Left, 2, Field::Flags, 0xc0010004u, 0x80010004u);
    transition(leftClear, 8, Eye::Left, 0, Field::Flags, 0xc0010004u, 0x80010004u);
    for (const auto& t : leftClear.transitions) assert(t.eye == Eye::Left);
}
void rejection_side_effects_and_attachment() {
    // Rejection happens after assignment, before refresh/children/final clear.
    for (const bool flagReject : {false, true}) {
        auto in = tree(2); in.owners[0].children = {1};
        auto& state = in.owners[0].state;
        state.flags = flagReject ? 0x40020020u : 0x40000020u;
        state.mask = flagReject ? 0 : 2; state.currentValue = 5; state.pendingValue = 9;
        in.views[0].mask = in.views[1].mask = 1;
        in.owners[1].state.currentValue = 10; in.owners[1].state.pendingValue = 11;
        const auto out = build_native_stereo_owner_plan(in); success(out);
        assert(out.eyes[0].ownerOrder.empty() && out.eyes[1].ownerOrder.empty());
        assert(out.transitions.size() == 1 && out.finalStates[0].flags == state.flags);
        transition(out, 0, Eye::Left, 0, Field::CurrentValue, 5, 9);
        assert(out.finalStates[1].currentValue == 10 && out.finalStates[1].flags == 0);
    }
    auto in = tree(); auto& state = in.owners[0].state;
    state.attachmentPresent = true; state.flags = 0x40480008u;
    state.currentValue = 3; state.pendingValue = 4;
    auto out = build_native_stereo_owner_plan(in); success(out);
    assert(out.eyes[0].ownerOrder.size() == 1 && out.eyes[1].ownerOrder.size() == 1);
    assert(out.finalStates[0].currentValue == 3 && out.finalStates[0].flags == 0x480008u);
    assert(out.transitions.size() == 1);
    transition(out, 0, Eye::Left, 0, Field::Flags, 0x40480008u, 0x480008u);
    // Attachment does not bypass the unconditional flag and mask gates.
    state.flags |= 0x20000u;
    out = build_native_stereo_owner_plan(in); success(out);
    assert(out.eyes[0].ownerOrder.empty() && out.transitions.empty());
    state.flags &= ~0x20000u; state.mask = 2; in.globalMask = 1;
    out = build_native_stereo_owner_plan(in); success(out);
    assert(out.eyes[1].ownerOrder.empty() && out.transitions.empty());
    in.globalMask = 3; in.views[0].mask = in.views[1].mask = 1;
    out = build_native_stereo_owner_plan(in); success(out);
    assert(out.eyes[0].ownerOrder.empty() && out.eyes[1].ownerOrder.empty());
}
void domain_and_structure_refusals() {
    for (const std::uint32_t flags : {0x400000u, 0x80000u, 0x480000u}) {
        auto in = tree(2); in.owners[0].children = {1}; in.owners[0].state.flags = 0x20000u;
        in.owners[1].state.flags = flags;
        const auto before = in;
        empty_refusal(build_native_stereo_owner_plan(in), Reason::UnsupportedOwnerFlags);
        unchanged(in, before);
    }
    empty_refusal(build_native_stereo_owner_plan(tree(0)), Reason::NodeLimit);
    empty_refusal(build_native_stereo_owner_plan(tree(4097)), Reason::NodeLimit);
    auto in = tree(); in.root = 1;
    empty_refusal(build_native_stereo_owner_plan(in), Reason::InvalidRoot);
    in.root = 0xffffffffu;
    empty_refusal(build_native_stereo_owner_plan(in), Reason::InvalidRoot);
    in = tree(2); in.owners[0].children = {2}; in.owners[0].state.flags = 0x20000u;
    empty_refusal(build_native_stereo_owner_plan(in), Reason::InvalidChildIndex);
    in.owners[0].children = {0xffffffffu};
    empty_refusal(build_native_stereo_owner_plan(in), Reason::InvalidChildIndex);
    // Cycles, root-parent edges, duplicates, shared children and unreachable
    // owners are refused even if hidden behind rejected parent state.
    for (unsigned shape = 0; shape < 6; ++shape) {
        in = tree(4); in.owners[0].state.flags = 0x20000u;
        if (shape == 0) { in.owners[0].children = {1}; in.owners[1].children = {0}; }
        if (shape == 1) { in.owners[0].children = {1, 1}; }
        if (shape == 2) { in.owners[0].children = {1, 2}; in.owners[1].children = {3}; in.owners[2].children = {3}; }
        if (shape == 3) { in.owners[0].children = {1}; in.owners[2].children = {3}; in.owners[3].children = {2}; }
        if (shape == 4) { in.owners[0].children = {1, 2}; }
        if (shape == 5) { in.owners[0].children = {0, 1, 2}; }
        empty_refusal(build_native_stereo_owner_plan(in), Reason::InvalidTree);
    }
    in = tree(2); in.owners[0].children = {1};
    in.owners[0].sourceIds = {0, 0};
    empty_refusal(build_native_stereo_owner_plan(in), Reason::DuplicateSourceId);
    in.owners[0].sourceIds = {0}; in.owners[1].sourceIds = {0};
    empty_refusal(build_native_stereo_owner_plan(in), Reason::DuplicateSourceId);
    in = tree(); in.owners[0].sourceIds.resize(65537);
    empty_refusal(build_native_stereo_owner_plan(in), Reason::SourceLimit);
    in = tree(2); in.owners[0].children = {1};
    in.owners[0].sourceIds.resize(32769); in.owners[1].sourceIds.resize(32768);
    empty_refusal(build_native_stereo_owner_plan(in), Reason::SourceLimit);
}
void bounded_depth_and_sources() {
    auto in = tree(4096);
    for (std::uint32_t i = 0; i < 4096; ++i) {
        if (i + 1 < 4096) in.owners[i].children = {i + 1};
        in.owners[i].state.flags = 0x40000000u;
        in.owners[i].state.pendingValue = i + 1;
        for (std::uint32_t j = 0; j < 16; ++j) in.owners[i].sourceIds.push_back(i * 16 + j);
    }
    const auto before = in;
    const auto out = build_native_stereo_owner_plan(in); success(out);
    assert(out.finalStates.size() == 4096 && out.transitions.size() == 12288);
    for (const auto& eye : out.eyes) {
        assert(eye.ownerOrder.size() == 4096 && eye.sourceOrder.size() == 65536);
        assert(eye.ownerOrder.front() == 0 && eye.ownerOrder.back() == 4095);
        assert(eye.sourceOrder.front() == 0 && eye.sourceOrder.back() == 65535);
    }
    transition(out, 8192, Eye::Left, 4095, Field::Flags, 0x40010000u, 0x10000u);
    transition(out, 12287, Eye::Left, 0, Field::Flags, 0x40010000u, 0x10000u);
    unchanged(in, before);
}
void allocation_failures() {
    auto in = tree(2); in.owners[0].children = {1}; in.owners[0].sourceIds = {1, 2};
    in.owners[1].sourceIds = {3}; in.owners[0].state.pendingValue = 17;
    const auto before = in;
    countAllocations = true; allocationCount = 0;
    auto baseline = build_native_stereo_owner_plan(in);
    countAllocations = false; success(baseline);
    const auto total = allocationCount;
    assert(total > 0 && total < 128); // Bounded sweep, including output allocations.
    for (std::size_t i = 1; i <= total; ++i) {
        failAllocation = i; allocationCount = 0; countAllocations = true;
        auto out = build_native_stereo_owner_plan(in);
        countAllocations = false;
        empty_refusal(out, Reason::AllocationFailure); unchanged(in, before);
    }
    failAllocation = 0;
    success(build_native_stereo_owner_plan(in));
}
}
int main() {
    static_assert(noexcept(build_native_stereo_owner_plan(NativeStereoOwnerPlanInput{})), "planner must not throw");
    right_only_and_order(); all_bits_masks(); shared_mutations_and_postorder();
    rejection_side_effects_and_attachment(); domain_and_structure_refusals();
    bounded_depth_and_sources(); allocation_failures();
    std::cout << "Owned stereo owner plan: ordered stereo admission, shared transitions, refusals, bounds and allocation failures passed\n";
}
#else
int main() { assert(!"owned stereo owner planner is not implemented"); }
#endif
