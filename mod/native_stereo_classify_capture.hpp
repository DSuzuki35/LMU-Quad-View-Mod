#pragma once
#pragma push_macro("near")
#pragma push_macro("far")
#undef near
#undef far
#include "native_stereo_classify.hpp"
#pragma pop_macro("far")
#pragma pop_macro("near")
#include <cfenv>
#include <cstddef>
#include <cstring>
#include <utility>
#include <xmmintrin.h>

namespace lmuqv {
inline constexpr size_t nativeStereoClassifyReadByteLimit = 448;
inline constexpr unsigned nativeStereoClassifySlotLimit = 512,
                          nativeStereoClassifyTotalLimit = 4096;
// Save x87 state as well as the exact SSE controls and sticky flags. All
// diagnostic arithmetic/formatting must live inside one of these guards.
class NativeStereoFpGuard {
  std::fenv_t environment_{};
  uint32_t mxcsr_;

public:
  explicit NativeStereoFpGuard(bool defaults = false) noexcept : mxcsr_(_mm_getcsr()) {
    std::fegetenv(&environment_);
    if (defaults) {
      std::fesetenv(FE_DFL_ENV);
      _mm_setcsr(0x1f80);
    }
  }
  ~NativeStereoFpGuard() {
    std::fesetenv(&environment_);
    _mm_setcsr(mxcsr_);
  }
  NativeStereoFpGuard(const NativeStereoFpGuard &) = delete;
  NativeStereoFpGuard &operator=(const NativeStereoFpGuard &) = delete;
};
// Native is outside the catch blocks: never retry it if it throws. Each
// bookkeeping scope restores the complete environment before forwarding.
template <class Begin, class Native, class End>
uint32_t invoke_native_stereo_classify(Begin &&begin, Native &&native, End &&end) {
  const uint32_t entry = _mm_getcsr();
  using Token = decltype(begin(entry));
  Token token{};
  {
    NativeStereoFpGuard guard;
    try {
      token = begin(entry);
    } catch (...) {
    }
  }
  const uint32_t returned = native();
  const uint32_t post = _mm_getcsr();
  {
    NativeStereoFpGuard guard;
    try {
      end(token, returned, post);
    } catch (...) {
    }
  }
  return returned;
}
struct NativeStereoClassifyCaptureArgs {
  const void *candidate = nullptr;
  const void *view = nullptr;
  const float *position = nullptr;
  bool skipDistanceBounds = false;
  float bias = 0;
  const void *mainViewPointer = nullptr;
};
// This transient snapshot includes pointer chain identities for bitwise
// stability checks. Persist only input, reason and bytesRead, never identities.
struct NativeStereoClassifySnapshot {
  bool complete = false;
  const char *reason = "incomplete snapshot";
  NativeStereoClassifyInput input{};
  uintptr_t positionIdentity = 0, ownerIdentity = 0;
  // Bits 0..9: candidate flags, radius, near, far, count, owner flags/scale,
  // position, view position/scale. Plane bits and added fields use the mapping below.
  uint32_t copiedFields = 0;
};
// Preserve the original plane/bias bits in existing local capture files.
inline constexpr unsigned nativeStereoClassifyBiasBit = 14;
inline constexpr unsigned nativeStereoClassifyViewModeBit = 17;
inline constexpr unsigned nativeStereoClassifySecondaryPositionBit = 18;
inline constexpr unsigned nativeStereoClassifySecondaryRadiusBit = 19;
inline constexpr unsigned native_stereo_classify_plane_bit(unsigned index) noexcept {
  return index < 4 ? 10 + index : 15 + (index - 4);
}
namespace native_classify_detail {
class Reader {
  bool (*readable_)(const void *, size_t);
  size_t &bytes_;
  const char *&reason_;

public:
  Reader(bool (*readable)(const void *, size_t), size_t &bytes, const char *&reason)
      : readable_(readable), bytes_(bytes), reason_(reason) {
  }
  template <class T> bool read(uintptr_t base, size_t offset, T &out) {
    if (bytes_ > nativeStereoClassifyReadByteLimit ||
        sizeof(T) > nativeStereoClassifyReadByteLimit - bytes_) {
      reason_ = "native byte limit";
      return false;
    }
    if (!base) {
      reason_ = "missing required pointer";
      return false;
    }
    const auto limit = (std::numeric_limits<uintptr_t>::max)();
    if (offset > limit - base || sizeof(T) > limit - (base + offset)) {
      reason_ = "address overflow";
      return false;
    }
    if (!readable_) {
      reason_ = "missing readable callback";
      return false;
    }
    // Charge attempted spans too, so callbacks and failures are bounded.
    bytes_ += sizeof(T);
    if (!readable_(reinterpret_cast<const void *>(base + offset), sizeof(T))) {
      reason_ = "unreadable native span";
      return false;
    }
    std::memcpy(&out, reinterpret_cast<const void *>(base + offset), sizeof(T));
    return true;
  }
};
inline bool equal_input(NativeStereoClassifyInput a, NativeStereoClassifyInput b) {
  a.candidateFlags &= ~0x70000u;
  b.candidateFlags &= ~0x70000u;
  // Fieldwise comparisons avoid struct padding and floating comparisons.
  return a.candidateFlags == b.candidateFlags && a.ownerFlags == b.ownerFlags &&
         std::memcmp(&a.ownerScale, &b.ownerScale, sizeof(float) * 4) == 0 &&
         a.planeCount == b.planeCount &&
         std::memcmp(&a.position, &b.position, sizeof(a.position)) == 0 &&
         std::memcmp(&a.viewPosition, &b.viewPosition, sizeof(a.viewPosition)) == 0 &&
         std::memcmp(&a.viewScale, &b.viewScale, sizeof(float) * 2) == 0 &&
         a.skipDistanceBounds == b.skipDistanceBounds &&
         std::memcmp(a.planes.data(), b.planes.data(), sizeof(a.planes)) == 0 &&
         a.viewMode == b.viewMode &&
         std::memcmp(&a.secondaryViewPosition, &b.secondaryViewPosition,
                     sizeof(a.secondaryViewPosition)) == 0 &&
         std::memcmp(&a.secondaryViewRadius, &b.secondaryViewRadius, sizeof(float)) == 0;
}
} // namespace native_classify_detail
inline NativeStereoClassifySnapshot
capture_native_stereo_classify(const NativeStereoClassifyCaptureArgs &args,
                               bool (*readable)(const void *, size_t), size_t &bytes) noexcept {
  NativeStereoFpGuard preserve;
  NativeStereoClassifySnapshot out;
  std::memcpy(&out.input.bias, &args.bias, sizeof(float));
  out.input.skipDistanceBounds = args.skipDistanceBounds;
  out.copiedFields = 1u << nativeStereoClassifyBiasBit;
  try {
    native_classify_detail::Reader r(readable, bytes, out.reason);
    const auto candidate = reinterpret_cast<uintptr_t>(args.candidate),
               view = reinterpret_cast<uintptr_t>(args.view);
    uintptr_t main = 0, parent = 0;
    if (!r.read(reinterpret_cast<uintptr_t>(args.mainViewPointer), 0, main))
      return out;
    if (!view || view != main) {
      out.reason = "not current main view";
      return out;
    }
    if (!r.read(view, 0x4b0, parent))
      return out;
    if (parent) {
      out.reason = "main view has parent";
      return out;
    }
    auto &in = out.input;
    const auto copy = [&](uintptr_t object, size_t offset, auto &value, unsigned bit) {
      if (!r.read(object, offset, value))
        return false;
      out.copiedFields |= 1u << bit;
      return true;
    };
    if (!copy(candidate, 0, in.candidateFlags, 0) || !copy(candidate, 0xdc, in.radius, 1) ||
        !copy(candidate, 0xfc, in.nearCoefficient, 2) ||
        !copy(candidate, 0x100, in.farCoefficient, 3) || !copy(candidate, 0x104, in.planeCount, 4))
      return out;
    if (args.skipDistanceBounds) {
      out.reason = "unsupported skip distance bounds";
      return out;
    }
    if (in.planeCount < 0 || in.planeCount > 6) {
      out.reason = "unsupported plane count";
      return out;
    }
    if (!r.read(candidate, 0x110, out.positionIdentity) ||
        !r.read(candidate, 0x130, out.ownerIdentity))
      return out;
    if (!args.position || out.positionIdentity != reinterpret_cast<uintptr_t>(args.position)) {
      out.reason = "actual position pointer differs";
      return out;
    }
    if (!copy(out.ownerIdentity, 0, in.ownerFlags, 5) ||
        !copy(out.ownerIdentity, 0xec, in.ownerScale, 6) ||
        !copy(out.positionIdentity, 0, in.position, 7) || !copy(view, 0x34, in.viewPosition, 8) ||
        !copy(view, 0x114, in.viewScale, 9))
      return out;
    if (in.planeCount > 4) {
      if (!copy(view, 0x3e8, in.viewMode, nativeStereoClassifyViewModeBit) ||
          !copy(view, 0x58, in.secondaryViewPosition, nativeStereoClassifySecondaryPositionBit) ||
          !copy(view, 0x100, in.secondaryViewRadius, nativeStereoClassifySecondaryRadiusBit))
        return out;
    }
    for (int32_t i = 0; i < in.planeCount; ++i) {
      if (!copy(view, 0x178 + size_t(i) * 16, in.planes[size_t(i)],
                native_stereo_classify_plane_bit(unsigned(i))))
        return out;
    }
    // The sixth plane is selected by every mode other than exactly one.
    if (in.planeCount == 5 && in.viewMode != 1) {
      if (!copy(view, 0x178 + 5 * 16, in.planes[5], native_stereo_classify_plane_bit(5)))
        return out;
    }
    out.complete = true;
    out.reason = "complete";
  } catch (...) {
    out.reason = "reader exception";
  }
  return out;
}
struct NativeStereoClassifyComparison {
  bool available = false, returnedMismatch = false, storedMismatch = false;
  const char *reason = "incomplete snapshot";
  NativeStereoClassifyResult predicted{};
};
inline NativeStereoClassifyComparison
compare_native_stereo_classify(const NativeStereoClassifySnapshot &pre,
                               const NativeStereoClassifySnapshot &post, uint32_t entryMxcsr,
                               uint32_t postMxcsr, uint32_t returned) noexcept {
  NativeStereoFpGuard defaults(true);
  NativeStereoClassifyComparison out;
  if (!pre.complete) {
    out.reason = pre.reason;
    return out;
  }
  if (!post.complete) {
    out.reason = post.reason;
    return out;
  }
  if ((entryMxcsr & ~0x3fu) != 0x1f80 || (postMxcsr & ~0x3fu) != 0x1f80) {
    out.reason = "nondefault MXCSR controls";
    return out;
  }
  if (pre.positionIdentity != post.positionIdentity || pre.ownerIdentity != post.ownerIdentity ||
      !native_classify_detail::equal_input(pre.input, post.input)) {
    out.reason = "input changed during native invocation";
    return out;
  }
  out.predicted = classify_native_stereo_numbers(pre.input);
  if (!out.predicted.available) {
    switch (out.predicted.reason) {
    case NativeStereoClassifyReason::NonFiniteInput:
      out.reason = "nonfinite input";
      break;
    case NativeStereoClassifyReason::InvalidDomain:
      out.reason = "invalid numeric domain";
      break;
    case NativeStereoClassifyReason::NonFiniteArithmetic:
      out.reason = "nonfinite model arithmetic";
      break;
    default:
      out.reason = "unsupported numeric mode";
      break;
    }
    return out;
  }
  out.available = true;
  out.reason = "copied numeric comparison only";
  out.returnedMismatch = returned != out.predicted.returnedFlags;
  out.storedMismatch = post.input.candidateFlags != out.predicted.storedCandidateFlags;
  return out;
}
} // namespace lmuqv
