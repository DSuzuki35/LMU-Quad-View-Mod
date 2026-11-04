#pragma once
#include "native_stereo_packets.hpp"
#include <cmath>
namespace lmuqv {
inline constexpr size_t nativeStereoSourceReadByteLimit = 24;
// Owned scalar values. arrayIdentity is only an ephemeral lookup key: consumers
// must anonymize it immediately, and must not publish values unless complete.
struct NativeStereoSourceSnapshot {
  bool complete = false;
  const char *reason = "incomplete snapshot";
  size_t bytesRead = 0;
  uintptr_t arrayIdentity = 0;
  int32_t count = 0;
  unsigned index = 0;
  float sortDistance = 0;
  bool lazyStatePresent = false;
};
inline NativeStereoSourceSnapshot capture_native_stereo_source(
    uintptr_t mesh, uintptr_t geometry,
    bool (*readable)(const void *, size_t)) noexcept {
  NativeStereoSourceSnapshot out;
  if (!mesh || !geometry) {
    out.reason = "missing required pointer";
    return out;
  }
  NativeStereoCaptureReason readReason = NativeStereoCaptureReason::complete;
  try {
    native_stereo_detail::Reader reader(readable, readReason, out.bytesRead);
    // Keep partial copies local. Neither mesh nor the reported array is read.
    int32_t count = 0;
    uint64_t array = 0, lazyState = 0;
    float sortDistance = 0;
    if (!reader.read(geometry, 0x168, count) ||
        !reader.read(geometry, 0x170, array)) {
      out.reason = native_stereo_capture_reason(readReason);
      return out;
    }
    if (count <= 0 || count > 16384) {
      out.reason = "invalid source record count";
      return out;
    }
    if (!array) {
      out.reason = "missing source array";
      return out;
    }
    constexpr uintptr_t stride = 0xe0;
    const auto limit = (std::numeric_limits<uintptr_t>::max)();
    const auto bytes = uintptr_t(count) * stride; // count is bounded above.
    if (array > limit || bytes > limit - uintptr_t(array)) {
      out.reason = "address overflow";
      return out;
    }
    const auto base = uintptr_t(array), end = base + bytes;
    if (mesh < base || mesh >= end) {
      out.reason = "mesh outside source array";
      return out;
    }
    const auto offset = mesh - base;
    if (offset % stride) {
      out.reason = "mesh is not a source record boundary";
      return out;
    }
    if (!reader.read(geometry, 0xf8, sortDistance) ||
        !reader.read(geometry, 0x138, lazyState)) {
      out.reason = native_stereo_capture_reason(readReason);
      return out;
    }
    if (!std::isfinite(sortDistance)) {
      out.reason = "non-finite sort distance";
      return out;
    }
    out.arrayIdentity = base;
    out.count = count;
    out.index = unsigned(offset / stride);
    out.sortDistance = sortDistance;
    out.lazyStatePresent = lazyState != 0;
    out.complete = true;
    out.reason = "complete";
  } catch (...) {
    out.reason = "diagnostic allocation or reader exception";
  }
  return out;
}
} // namespace lmuqv
