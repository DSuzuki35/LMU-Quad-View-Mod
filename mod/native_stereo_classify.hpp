#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace lmuqv {
struct NativeStereoClassifyFloat3 { float x{}, y{}, z{}; };
struct NativeStereoClassifyPlane { float x{}, y{}, z{}, w{}; };
struct NativeStereoClassifyInput {
    std::uint32_t candidateFlags{}, ownerFlags{};
    float ownerScale{1}, nearCoefficient{}, farCoefficient{1}, radius{};
    std::int32_t planeCount{};
    NativeStereoClassifyFloat3 position{}, viewPosition{};
    float viewScale{1}, bias{};
    bool skipDistanceBounds{};
    std::array<NativeStereoClassifyPlane, 6> planes{};
    std::int32_t viewMode{};
    NativeStereoClassifyFloat3 secondaryViewPosition{};
    float secondaryViewRadius{};
};
enum class NativeStereoClassifyReason {
    None, UnsupportedMode, NonFiniteInput, InvalidDomain, NonFiniteArithmetic
};
struct NativeStereoClassifyResult {
    bool available{};
    NativeStereoClassifyReason reason{NativeStereoClassifyReason::UnsupportedMode};
    std::uint32_t returnedFlags{}, storedCandidateFlags{};
};

// Offline model of the restricted 0x5306A0 classifier, on copied numbers only.
// This is not a visibility/scheduling decision and does not access native objects.
// Future capture must take position from the classifier's R8 argument (normally
// the pointer at candidate+0x110), not sorting coordinates at candidate+0xEC.
//
// Domain: planeCount 0..6, normal distance bounds; all scalar/vector inputs and
// declared planes finite; radius/near/bias >= 0; scales/far > 0; final
// float near*viewScale < far*viewScale. Input validation is deliberately upfront,
// including active planes bypassed by owner sign or an earlier return. Unused
// planes are ignored, except the selected special plane for counts > 4.
// Those counts additionally require finite secondary position/radius, radius >= 0.
// Added fields are ignored for counts 0..4. This is not an engine-domain claim.
// Only reached arithmetic is evaluated; any nonfinite intermediate refuses.
// Unavailable results have zero flag fields: always check available/reason.
//
// Requires IEEE binary32 round-to-nearest scalar float arithmetic, default
// floating-point environment, no fast-math/reassociation and no contraction.
// Tested with clang -ffp-contract=off at O0/O2; Clang's local pragma additionally
// disables contraction here. Other toolchains must disable contraction explicitly.
inline NativeStereoClassifyResult classify_native_stereo_numbers(
    const NativeStereoClassifyInput& in) noexcept {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    static_assert(std::numeric_limits<float>::is_iec559 &&
                  std::numeric_limits<float>::digits == 24 && sizeof(float) == 4,
                  "classifier model requires IEEE binary32 float");
    using Reason = NativeStereoClassifyReason;
    const auto refuse = [](Reason reason) noexcept {
        return NativeStereoClassifyResult{false, reason, 0, 0};
    };
    if (in.skipDistanceBounds || in.planeCount < 0 || in.planeCount > 6)
        return refuse(Reason::UnsupportedMode);
    if (!std::isfinite(in.ownerScale) || !std::isfinite(in.nearCoefficient) ||
        !std::isfinite(in.farCoefficient) || !std::isfinite(in.radius) ||
        !std::isfinite(in.viewScale) || !std::isfinite(in.bias) ||
        !std::isfinite(in.position.x) || !std::isfinite(in.position.y) ||
        !std::isfinite(in.position.z) || !std::isfinite(in.viewPosition.x) ||
        !std::isfinite(in.viewPosition.y) || !std::isfinite(in.viewPosition.z))
        return refuse(Reason::NonFiniteInput);
    for (std::int32_t i = 0; i < in.planeCount; ++i) {
        const auto& plane = in.planes[static_cast<std::size_t>(i)];
        if (!std::isfinite(plane.x) || !std::isfinite(plane.y) ||
            !std::isfinite(plane.z) || !std::isfinite(plane.w))
            return refuse(Reason::NonFiniteInput);
    }
    // Count five may select plane five even though it is outside the normal loop.
    const std::size_t specialIndex = in.viewMode == 1 ? 4 : 5;
    if (in.planeCount > 4) {
        const auto& plane = in.planes[specialIndex];
        if (!std::isfinite(plane.x) || !std::isfinite(plane.y) ||
            !std::isfinite(plane.z) || !std::isfinite(plane.w) ||
            !std::isfinite(in.secondaryViewPosition.x) ||
            !std::isfinite(in.secondaryViewPosition.y) ||
            !std::isfinite(in.secondaryViewPosition.z) ||
            !std::isfinite(in.secondaryViewRadius))
            return refuse(Reason::NonFiniteInput);
        if (in.secondaryViewRadius < 0) return refuse(Reason::InvalidDomain);
    }
    if (in.ownerScale <= 0 || in.viewScale <= 0 || in.farCoefficient <= 0 ||
        in.nearCoefficient < 0 || in.radius < 0 || in.bias < 0)
        return refuse(Reason::InvalidDomain);

    const std::uint32_t f = in.candidateFlags & 0xFFF8FFFFu;
    const auto classified = [f](std::uint32_t bits) noexcept {
        return NativeStereoClassifyResult{true, Reason::None, f | bits, f | bits};
    };
    const float dx = in.position.x - in.viewPosition.x;
    const float dy = in.position.y - in.viewPosition.y;
    const float dz = in.position.z - in.viewPosition.z;
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
        return refuse(Reason::NonFiniteArithmetic);
    const float yy = dy * dy;
    const float xx = dx * dx;
    const float zz = dz * dz;
    if (!std::isfinite(yy) || !std::isfinite(xx) || !std::isfinite(zz))
        return refuse(Reason::NonFiniteArithmetic);
    const float yx = yy + xx;
    if (!std::isfinite(yx)) return refuse(Reason::NonFiniteArithmetic);
    const float squaredDistance = yx + zz;
    if (!std::isfinite(squaredDistance)) return refuse(Reason::NonFiniteArithmetic);
    const float distance = std::sqrt(squaredDistance);
    if (!std::isfinite(distance)) return refuse(Reason::NonFiniteArithmetic);
    const float floor = in.viewScale * in.bias;
    if (!std::isfinite(floor)) return refuse(Reason::NonFiniteArithmetic);
    const float d = distance < floor ? floor : distance;

    const float far = ((f & 1u) && (in.ownerFlags & 4u))
        ? in.farCoefficient : in.ownerScale * in.farCoefficient;
    const float near = in.ownerScale * in.nearCoefficient;
    if (!std::isfinite(far) || !std::isfinite(near))
        return refuse(Reason::NonFiniteArithmetic);
    const float effectiveFar = far * in.viewScale;
    const float effectiveNear = near * in.viewScale;
    if (!std::isfinite(effectiveFar) || !std::isfinite(effectiveNear))
        return refuse(Reason::NonFiniteArithmetic);
    if (!(effectiveNear < effectiveFar)) return refuse(Reason::InvalidDomain);
    if (d > effectiveFar) return classified(0x50000u);
    if (d <= effectiveNear) return classified(0x50000u);
    if (in.planeCount > 4) {
        const auto& plane = in.planes[specialIndex];
        const float py = in.position.y * plane.y;
        const float px = in.position.x * plane.x;
        const float pz = in.position.z * plane.z;
        if (!std::isfinite(py) || !std::isfinite(px) || !std::isfinite(pz))
            return refuse(Reason::NonFiniteArithmetic);
        const float xy = py + px;
        const float zw = pz + plane.w;
        if (!std::isfinite(xy) || !std::isfinite(zw))
            return refuse(Reason::NonFiniteArithmetic);
        const float dot = xy + zw;
        if (!std::isfinite(dot)) return refuse(Reason::NonFiniteArithmetic);
        // The special-plane path has distinct native return and stored flags.
        if (dot > in.radius) return {true, Reason::None, 0x40000u, f};
        const float sphereRadius = in.radius + in.secondaryViewRadius;
        if (!std::isfinite(sphereRadius)) return refuse(Reason::NonFiniteArithmetic);
        const float sx = in.position.x - in.secondaryViewPosition.x;
        const float sy = in.position.y - in.secondaryViewPosition.y;
        const float sz = in.position.z - in.secondaryViewPosition.z;
        if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz))
            return refuse(Reason::NonFiniteArithmetic);
        const float syy = sy * sy;
        const float sxx = sx * sx;
        const float szz = sz * sz;
        if (!std::isfinite(syy) || !std::isfinite(sxx) || !std::isfinite(szz))
            return refuse(Reason::NonFiniteArithmetic);
        const float syx = syy + sxx;
        if (!std::isfinite(syx)) return refuse(Reason::NonFiniteArithmetic);
        const float distanceSquared = syx + szz;
        if (!std::isfinite(distanceSquared)) return refuse(Reason::NonFiniteArithmetic);
        const float radiusSquared = sphereRadius * sphereRadius;
        if (!std::isfinite(radiusSquared)) return refuse(Reason::NonFiniteArithmetic);
        if (distanceSquared > radiusSquared) return classified(0x40000u);
    }
    // Sign-bit test avoids implementation-defined uint32 -> int32 conversion.
    if ((in.ownerFlags & 0x80000000u) || in.planeCount == 0) return classified(0);
    for (std::int32_t i = 0; i < in.planeCount; ++i) {
        const auto& plane = in.planes[static_cast<std::size_t>(i)];
        const float py = in.position.y * plane.y;
        const float px = in.position.x * plane.x;
        const float pz = in.position.z * plane.z;
        if (!std::isfinite(py) || !std::isfinite(px) || !std::isfinite(pz))
            return refuse(Reason::NonFiniteArithmetic);
        const float xy = py + px;
        const float zw = pz + plane.w;
        if (!std::isfinite(xy) || !std::isfinite(zw))
            return refuse(Reason::NonFiniteArithmetic);
        const float dot = xy + zw;
        if (!std::isfinite(dot)) return refuse(Reason::NonFiniteArithmetic);
        if (dot > in.radius) return classified(0x40000u);
        if (dot > -in.radius) return classified(0x20000u);
    }
    return classified(0);
}
} // namespace lmuqv
