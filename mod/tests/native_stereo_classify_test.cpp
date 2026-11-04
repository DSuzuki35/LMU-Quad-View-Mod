#include "../native_stereo_classify.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

using namespace lmuqv;
using Input = NativeStereoClassifyInput;
using Reason = NativeStereoClassifyReason;
static unsigned checks = 0;
static void require(bool ok, const std::string& message) {
    ++checks;
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
static Input base() {
    Input in{};
    in.position = {5, 0, 0};
    in.nearCoefficient = 1;
    in.farCoefficient = 10;
    in.radius = 1;
    return in;
}
static void flags(const Input& in, std::uint32_t want, const char* label) {
    const auto result = classify_native_stereo_numbers(in);
    require(result.available && result.reason == Reason::None,
            std::string(label) + ": unavailable");
    require(result.returnedFlags == want && result.storedCandidateFlags == want,
            std::string(label) + ": wrong returned/stored flags");
}
static void refused(const Input& in, Reason why, const char* label) {
    const auto result = classify_native_stereo_numbers(in);
    require(!result.available && result.reason == why,
            std::string(label) + ": wrong refusal");
    require(result.returnedFlags == 0 && result.storedCandidateFlags == 0,
            std::string(label) + ": unavailable flags are not empty");
}

static void distance_boundaries_and_flags() {
    auto in = base();
    flags(in, 0, "inside distance bounds");
    in.position.x = 10;
    flags(in, 0, "far equality passes");
    in.position.x = std::nextafter(10.0f, 11.0f);
    flags(in, 0x50000, "strictly beyond far");
    in.position.x = 1;
    flags(in, 0x50000, "near equality rejected");
    in.position.x = std::nextafter(1.0f, 2.0f);
    flags(in, 0, "strictly above near");
    in.position = {0, 0, 0};
    flags(in, 0x50000, "below near");
    in.bias = 5;
    flags(in, 0, "bias supplies distance floor");
    in.bias = 11;
    flags(in, 0x50000, "bias can exceed far");
    in = base();
    in.viewPosition = {3, -3, -4};
    in.position = {3, 0, 0};
    in.farCoefficient = 5;
    flags(in, 0, "translated 3-4-5 distance");
    in.viewScale = 2;
    in.position.x = 3;
    in.position.y = 3;
    in.position.z = 4;
    flags(in, 0, "view-scaled far equality");
    in = base();
    in.ownerScale = 2;
    in.position.x = 15;
    in.candidateFlags = 1;
    flags(in, 1, "ordinary owner scaling");
    in.ownerFlags = 4;
    flags(in, 0x50001, "candidate one and owner four skip far scaling");
    in.candidateFlags = 0;
    flags(in, 0, "owner four alone retains far scaling");
    in.position.x = 2;
    flags(in, 0x50000, "near always uses owner scaling");
    in = base(); in.ownerScale = std::numeric_limits<float>::max();
    in.nearCoefficient = 0; in.candidateFlags = 1; in.ownerFlags = 4;
    flags(in, 1, "special far branch avoids unused owner-scale overflow");
    in = base();
    in.candidateFlags = 0xFFFFFFFFu;
    flags(in, 0xFFF8FFFFu, "clear only classifier flag bits");
    in.position.x = 20;
    flags(in, 0xFFFDFFFFu, "distance preserves unrelated bits");
}

static void plane_order_and_boundaries() {
    auto in = base();
    in.planeCount = 1;
    in.planes[0] = {0, 0, 0, -2};
    flags(in, 0, "inside plane");
    in.planes[0].w = -1;
    flags(in, 0, "negative radius equality continues");
    in.planes[0].w = std::nextafter(-1.0f, 0.0f);
    flags(in, 0x20000, "above negative radius intersects");
    in.planes[0].w = 1;
    flags(in, 0x20000, "positive radius equality intersects");
    in.planes[0].w = std::nextafter(1.0f, 2.0f);
    flags(in, 0x40000, "above positive radius rejects");
    in.radius = 0;
    in.planes[0].w = 0;
    flags(in, 0, "zero radius equality continues");
    in = base();
    in.planeCount = 2;
    in.planes[0].w = 0;
    in.planes[1].w = 2;
    flags(in, 0x20000, "first intersection ignores later outside");
    in.planes[0].w = 2;
    in.planes[1].w = 0;
    flags(in, 0x40000, "first outside rejects before intersection");
    in.planes[0].w = -1;
    flags(in, 0x20000, "inside proceeds to next intersection");
    in.planeCount = 4;
    for (auto& plane : in.planes) plane.w = -2;
    flags(in, 0, "four planes exhausted");
    in.planes[3].w = 2;
    flags(in, 0x40000, "fourth plane visited");
    in.ownerFlags = 0x80000000u;
    flags(in, 0, "negative owner bypasses planes");
    in.position.x = 20;
    flags(in, 0x50000, "negative owner still checks distance");
    in = base();
    in.candidateFlags = 0xFFFFFFFFu;
    in.planeCount = 1;
    flags(in, 0xFFFAFFFFu, "intersection preserves unrelated bits");
    in.planes[0].w = 2;
    flags(in, 0xFFFCFFFFu, "outside preserves unrelated bits");
}

static void scalar_operation_order() {
    auto in = base();
    // ((4096^2 + 1^2) + 1.5^2) rounds to 16777218;
    // its correctly rounded float sqrt is 4096. Adding small squares first
    // would instead round to 16777220, with sqrt one float above 4096.
    in.position = {1, 4096, 1.5f};
    in.farCoefficient = 4096;
    flags(in, 0, "distance uses y squared then x squared then z squared");
    in.farCoefficient = 10000;
    in.position = {1, 4096, 1};
    in.planeCount = 1;
    in.radius = 0.5f;
    in.planes[0] = {1, 4096, -16777216, -1};
    flags(in, 0x20000, "dot keeps both parenthesized sums");
    in.position = {-0x1.000004p0f, 0x1.000002p0f, 0};
    in.planes[0] = {1, 0x1.000002p0f, 0, 0};
    in.radius = 0;
    flags(in, 0, "multiply/add contraction must stay disabled");
}

static void restricted_domain() {
    auto in = base();
    in.skipDistanceBounds = true;
    refused(in, Reason::UnsupportedMode, "skip-distance mode");
    for (const auto count : {-1, 7, std::numeric_limits<std::int32_t>::max()}) {
        in = base(); in.planeCount = count;
        refused(in, Reason::UnsupportedMode, "unsupported plane count");
    }
    for (const float bad : {0.0f, -1.0f}) {
        in = base(); in.ownerScale = bad;
        refused(in, Reason::InvalidDomain, "owner scale must be positive");
        in = base(); in.viewScale = bad;
        refused(in, Reason::InvalidDomain, "view scale must be positive");
        in = base(); in.farCoefficient = bad;
        refused(in, Reason::InvalidDomain, "far coefficient must be positive");
    }
    in = base(); in.radius = -1;
    refused(in, Reason::InvalidDomain, "negative radius");
    in = base(); in.nearCoefficient = -1;
    refused(in, Reason::InvalidDomain, "negative near coefficient");
    in = base(); in.bias = -1;
    refused(in, Reason::InvalidDomain, "negative bias");
    for (const float near : {10.0f, 11.0f}) {
        in = base(); in.nearCoefficient = near;
        refused(in, Reason::InvalidDomain, "effective near must be below far");
    }
    in = base(); in.viewScale = std::numeric_limits<float>::denorm_min();
    in.nearCoefficient = 0; in.farCoefficient = 0.25f;
    refused(in, Reason::InvalidDomain, "underflow-collapsed effective bounds");
    in = base(); in.nearCoefficient = 0; in.radius = 0; in.bias = 0;
    flags(in, 0, "zero nonnegative coefficients allowed");
}

static void finite_input_validation() {
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity()}) {
        for (unsigned field = 0; field < 12; ++field) {
            auto in = base();
            const std::array<float*, 12> fields{{&in.ownerScale,
                &in.nearCoefficient, &in.farCoefficient, &in.radius,
                &in.viewScale, &in.bias, &in.position.x, &in.position.y,
                &in.position.z, &in.viewPosition.x, &in.viewPosition.y,
                &in.viewPosition.z}};
            *fields[field] = bad;
            refused(in, Reason::NonFiniteInput, "nonfinite scalar/vector input");
        }
        for (unsigned plane = 0; plane < 4; ++plane) {
            for (unsigned component = 0; component < 4; ++component) {
                auto in = base(); in.planeCount = 4;
                const std::array<float*, 4> fields{{&in.planes[plane].x,
                    &in.planes[plane].y, &in.planes[plane].z, &in.planes[plane].w}};
                *fields[component] = bad;
                refused(in, Reason::NonFiniteInput, "nonfinite active plane");
                in.ownerFlags = 0x80000000u;
                refused(in, Reason::NonFiniteInput, "upfront domain also validates bypassed planes");
                in.planeCount = static_cast<std::int32_t>(plane);
                in.ownerFlags = 0;
                flags(in, plane == 0 ? 0u : 0x20000u, "unused plane ignored");
            }
        }
    }
}

static void intermediate_overflow() {
    const float high = std::numeric_limits<float>::max();
    auto in = base();
    for (unsigned axis = 0; axis < 3; ++axis) {
        in = base();
        const std::array<float*, 3> positions{{&in.position.x, &in.position.y, &in.position.z}};
        const std::array<float*, 3> views{{&in.viewPosition.x, &in.viewPosition.y, &in.viewPosition.z}};
        *positions[axis] = high; *views[axis] = -high;
        refused(in, Reason::NonFiniteArithmetic, "position subtraction overflow");
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        in = base();
        const std::array<float*, 3> axes{{&in.position.x, &in.position.y, &in.position.z}};
        *axes[axis] = 0x1p64f;
        refused(in, Reason::NonFiniteArithmetic, "distance square overflow");
    }
    in = base(); in.position = {0x1.8p63f, 0x1.8p63f, 0};
    refused(in, Reason::NonFiniteArithmetic, "distance xy square sum overflow");
    in.position = {0x1.4p63f, 0x1.4p63f, 0x1.4p63f};
    refused(in, Reason::NonFiniteArithmetic, "distance final square sum overflow");
    in = base(); in.viewScale = 2; in.bias = high;
    refused(in, Reason::NonFiniteArithmetic, "bias floor overflow");
    in = base(); in.ownerScale = high;
    refused(in, Reason::NonFiniteArithmetic, "owner-scaled far overflow");
    in = base(); in.ownerScale = 2; in.nearCoefficient = high;
    refused(in, Reason::NonFiniteArithmetic, "owner-scaled near overflow");
    in = base(); in.farCoefficient = high; in.viewScale = 2;
    refused(in, Reason::NonFiniteArithmetic, "view-scaled far overflow");
    in = base(); in.ownerScale = 0.5f; in.nearCoefficient = high; in.farCoefficient = 1; in.viewScale = 4;
    refused(in, Reason::NonFiniteArithmetic, "view-scaled near overflow");
    for (unsigned axis = 0; axis < 3; ++axis) {
        in = base(); in.planeCount = 1; in.position = {2, 2, 2};
        const std::array<float*, 3> axes{{&in.planes[0].x, &in.planes[0].y, &in.planes[0].z}};
        *axes[axis] = high;
        refused(in, Reason::NonFiniteArithmetic, "plane product overflow");
    }
    in = base(); in.planeCount = 1; in.position = {1, 1, 1};
    in.planes[0] = {high, high, 0, 0};
    refused(in, Reason::NonFiniteArithmetic, "plane xy sum overflow");
    in.planes[0] = {0, 0, high, high};
    refused(in, Reason::NonFiniteArithmetic, "plane zw sum overflow");
    in.planes[0] = {0, high, high, 0};
    refused(in, Reason::NonFiniteArithmetic, "plane final sum overflow");
    in.planeCount = 2; in.planes[1] = in.planes[0]; in.planes[0] = {};
    flags(in, 0x20000, "intersection bypasses later arithmetic overflow");
    in.planes[0].w = 2;
    flags(in, 0x40000, "outside bypasses later arithmetic overflow");
    in.planes[0] = in.planes[1]; in.ownerFlags = 0x80000000u;
    flags(in, 0, "owner sign bypasses arithmetic overflow");
    in.ownerFlags = 0; in.farCoefficient = 0.5f; in.nearCoefficient = 0;
    flags(in, 0x50000, "distance rejection bypasses dot overflow");
}

static Input six_base(int count = 6, int mode = 1) {
    auto in = base();
    in.planeCount = count;
    in.viewMode = mode;
    in.secondaryViewPosition = in.position;
    in.secondaryViewRadius = 2;
    for (auto &p : in.planes)
        p.w = -2;
    return in;
}
static void six_planes() {
    for (int count : {5, 6})
        for (int mode : {1, 0, -9, INT32_MAX}) {
            auto in = six_base(count, mode);
            flags(in, 0, "five/six planes inside");
            in.candidateFlags = 0x1237abcd;
            in.planes[mode == 1 ? 4 : 5].w = 2;
            auto out = classify_native_stereo_numbers(in);
            require(out.available && out.returnedFlags == 0x40000 &&
                        out.storedCandidateFlags == (in.candidateFlags & 0xfff8ffffu),
                    "special literal return and cleared store");
            in.ownerFlags = 0x80000000;
            out = classify_native_stereo_numbers(in);
            require(out.available && out.returnedFlags == 0x40000, "sign cannot bypass special");
            in.position.x = 20;
            flags(in, (in.candidateFlags & 0xfff8ffffu) | 0x50000, "distance first");
        }
    auto in = six_base(5, 1);
    in.planes[5].w = std::numeric_limits<float>::quiet_NaN();
    flags(in, 0, "count five mode one ignores sixth");
    in.viewMode = 0;
    refused(in, Reason::NonFiniteInput, "selected sixth validated for count five");
    in = six_base();
    in.ownerFlags = 0x80000000;
    for (float w : {std::nextafter(1.f, 0.f), 1.f}) {
        in.planes[4].w = w;
        flags(in, 0, "special boundary continues");
    }
    in.planes[4].w = std::nextafter(1.f, 2.f);
    auto out = classify_native_stereo_numbers(in);
    require(out.available && out.returnedFlags == 0x40000 && out.storedCandidateFlags == 0,
            "adjacent special rejects");
    in = six_base();
    in.ownerFlags = 0x80000000;
    for (float x : {8.f, std::nextafter(8.f, 7.f)}) {
        in.secondaryViewPosition.x = x;
        flags(in, 0, "sphere equality and inside");
    }
    in.secondaryViewPosition.x = std::nextafter(8.f, 9.f);
    flags(in, 0x40000, "sphere adjacent outside before sign");
    in.ownerFlags = 0;
    in.planes[0].w = 0;
    flags(in, 0x40000, "sphere before normal intersection");
    in = six_base(5, 0);
    in.planes[4].w = 2;
    flags(in, 0x40000, "normal fifth");
    in = six_base();
    in.planes[5].w = 2;
    flags(in, 0x40000, "normal sixth");
    in.planes[0].w = 0;
    flags(in, 0x20000, "first intersection");
    in = six_base();
    in.position = {1, 4096, 1};
    in.farCoefficient = 10000;
    in.secondaryViewPosition = in.position;
    in.radius = 0.5f;
    in.ownerFlags = 0x80000000;
    in.planes[4] = {1, 4096, -16777216, -1};
    flags(in, 0, "special grouped dot");
    in.position = {-0x1.000004p0f, 0x1.000002p0f, 0};
    in.secondaryViewPosition = in.position;
    in.planes[4] = {1, 0x1.000002p0f, 0, 0};
    in.radius = 0;
    flags(in, 0, "special no contraction");
    in = six_base();
    in.ownerFlags = 0x80000000;
    in.position = {1, 4096, 1.5f};
    in.secondaryViewPosition = {};
    in.farCoefficient = 10000;
    in.radius = 4096;
    in.secondaryViewRadius = 0;
    flags(in, 0x40000, "sphere squared distance without sqrt rounding");
    // Both small squares disappear in (y*y + x*x) + z*z. Adding them
    // together first would reject this exact radius-squared boundary.
    in.position = {0.5f, 2048, 0.5f};
    in.radius = 2048;
    flags(in, 0, "secondary sphere preserves y/x then z grouping");
}
static void six_validation() {
    const float high = std::numeric_limits<float>::max();
    for (float bad :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()}) {
        for (unsigned i = 0; i < 4; ++i) {
            auto in = six_base();
            std::array<float *, 4> fields{&in.secondaryViewPosition.x, &in.secondaryViewPosition.y,
                                          &in.secondaryViewPosition.z, &in.secondaryViewRadius};
            *fields[i] = bad;
            refused(in, Reason::NonFiniteInput, "new fields validated upfront");
            for (int count = 0; count <= 4; ++count) {
                in.planeCount = count;
                in.planes[4].w = bad;
                in.planes[5].w = bad;
                flags(in, 0, "old counts ignore added fields");
            }
        }
        for (int plane : {4, 5})
            for (unsigned i = 0; i < 4; ++i) {
                auto in = six_base();
                std::array<float *, 4> fields{&in.planes[plane].x, &in.planes[plane].y,
                                              &in.planes[plane].z, &in.planes[plane].w};
                *fields[i] = bad;
                refused(in, Reason::NonFiniteInput, "new declared planes upfront");
            }
    }
    auto in = six_base();
    in.secondaryViewRadius = -1;
    refused(in, Reason::InvalidDomain, "negative secondary radius");
    for (unsigned axis = 0; axis < 3; ++axis) {
        in = six_base();
        in.position = {2, 2, 2};
        std::array<float *, 3> fields{&in.planes[4].x, &in.planes[4].y, &in.planes[4].z};
        *fields[axis] = high;
        refused(in, Reason::NonFiniteArithmetic, "special product overflow");
    }
    for (auto p :
         {NativeStereoClassifyPlane{high, high, 0, 0}, {0, 0, high, high}, {0, high, high, 0}}) {
        in = six_base();
        in.position = {1, 1, 1};
        in.planes[4] = p;
        refused(in, Reason::NonFiniteArithmetic, "special sums overflow");
    }
    in = six_base();
    in.radius = high;
    in.secondaryViewRadius = high;
    refused(in, Reason::NonFiniteArithmetic, "secondary radius sum overflow");
    in = six_base();
    in.secondaryViewRadius = high;
    refused(in, Reason::NonFiniteArithmetic, "secondary radius square overflow");
    for (unsigned axis = 0; axis < 3; ++axis) {
        in = six_base();
        in.position = {0, 0, 0};
        in.bias = 5;
        std::array<float *, 3> fields{&in.secondaryViewPosition.x, &in.secondaryViewPosition.y,
                                      &in.secondaryViewPosition.z};
        *fields[axis] = high;
        refused(in, Reason::NonFiniteArithmetic, "secondary square overflow");
    }
    in = six_base();
    in.position = {high, 0, 0};
    in.viewPosition = in.position;
    in.bias = 5;
    in.secondaryViewPosition.x = -high;
    refused(in, Reason::NonFiniteArithmetic, "secondary subtraction overflow");
    in = six_base();
    in.secondaryViewPosition = {0x1.8p63f, 0x1.8p63f, 0};
    refused(in, Reason::NonFiniteArithmetic, "secondary xy overflow");
    in.secondaryViewPosition = {0x1.4p63f, 0x1.4p63f, 0x1.4p63f};
    refused(in, Reason::NonFiniteArithmetic, "secondary final sum overflow");
    in = six_base();
    in.secondaryViewRadius = high;
    in.planes[4].w = 2;
    auto out = classify_native_stereo_numbers(in);
    require(out.available && out.returnedFlags == 0x40000 && out.storedCandidateFlags == 0,
            "special skips sphere overflow");
    in.farCoefficient = 1;
    in.nearCoefficient = 0;
    in.planes[4].x = high;
    flags(in, 0x50000, "distance skips special overflow");
}

static bool equal_input(const Input &a, const Input &b) {
    if (a.candidateFlags != b.candidateFlags || a.ownerFlags != b.ownerFlags ||
        a.ownerScale != b.ownerScale || a.nearCoefficient != b.nearCoefficient ||
        a.farCoefficient != b.farCoefficient || a.radius != b.radius ||
        a.planeCount != b.planeCount || a.position.x != b.position.x ||
        a.position.y != b.position.y || a.position.z != b.position.z ||
        a.viewPosition.x != b.viewPosition.x || a.viewPosition.y != b.viewPosition.y ||
        a.viewPosition.z != b.viewPosition.z || a.viewScale != b.viewScale ||
        a.viewMode != b.viewMode || a.secondaryViewRadius != b.secondaryViewRadius ||
        a.secondaryViewPosition.x != b.secondaryViewPosition.x ||
        a.secondaryViewPosition.y != b.secondaryViewPosition.y ||
        a.secondaryViewPosition.z != b.secondaryViewPosition.z || a.bias != b.bias ||
        a.skipDistanceBounds != b.skipDistanceBounds)
        return false;
    for (std::size_t i = 0; i < a.planes.size(); ++i) {
        if (a.planes[i].x != b.planes[i].x || a.planes[i].y != b.planes[i].y ||
            a.planes[i].z != b.planes[i].z || a.planes[i].w != b.planes[i].w)
            return false;
    }
    return true;
}
static void immutable_and_repeatable() {
    auto in = six_base();
    in.candidateFlags = 0x1237ABCD;
    const auto before = in;
    const auto first = classify_native_stereo_numbers(in);
    for (unsigned i = 0; i < 100; ++i) {
        const auto next = classify_native_stereo_numbers(in);
        require(next.available == first.available && next.reason == first.reason &&
                    next.returnedFlags == first.returnedFlags &&
                    next.storedCandidateFlags == first.storedCandidateFlags,
                "repeated evaluation changed output");
        require(equal_input(in, before), "evaluation mutated owned inputs");
    }
}
int main() {
    distance_boundaries_and_flags();
    plane_order_and_boundaries();
    scalar_operation_order();
    restricted_domain();
    finite_input_validation();
    intermediate_overflow();
    six_planes();
    six_validation();
    immutable_and_repeatable();
    std::cout << "native stereo numeric classifier: " << checks << " checks passed\n";
}
