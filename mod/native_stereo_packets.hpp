#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>
namespace lmuqv {
inline constexpr unsigned nativeStereoWorkLimit = 16384,
                          nativeStereoPacketLimit = 4096;
// Each work unit copies at most one 24-byte vector header or one pointer.
// Each emitted mesh copies at most 24 scalar bytes. This is a conservative
// upper bound on copied native bytes, independent of sparse/empty nesting.
inline constexpr size_t nativeStereoReadByteLimit =
    size_t(nativeStereoWorkLimit) * 24 + size_t(nativeStereoPacketLimit) * 24;
enum class NativeStereoCaptureReason {
  complete,
  missingPointer,
  missingReadable,
  addressOverflow,
  malformedVector,
  unreadable,
  workLimit,
  packetLimit,
  allocationFailure
};
inline const char *
native_stereo_capture_reason(NativeStereoCaptureReason reason) {
  switch (reason) {
  case NativeStereoCaptureReason::complete:
    return "complete";
  case NativeStereoCaptureReason::missingPointer:
    return "missing required pointer";
  case NativeStereoCaptureReason::missingReadable:
    return "missing readable callback";
  case NativeStereoCaptureReason::addressOverflow:
    return "address overflow";
  case NativeStereoCaptureReason::malformedVector:
    return "malformed or unaligned vector";
  case NativeStereoCaptureReason::unreadable:
    return "unreadable native span";
  case NativeStereoCaptureReason::workLimit:
    return "pointer traversal work limit";
  case NativeStereoCaptureReason::packetLimit:
    return "packet output limit";
  case NativeStereoCaptureReason::allocationFailure:
    return "diagnostic allocation or reader exception";
  }
  return "unknown";
}
// Identities below are only observations. They never retain a native resource.
// Consumers must anonymize them before serialization and must check complete.
struct NativeStereoPacket {
  uintptr_t mesh = 0, geometry = 0;
  uint32_t meshFlags = 0, geometryFlags = 0;
  bool meshCallback = false;
  unsigned bucketIndex = 0, itemIndex = 0, meshIndex = 0;
};
struct NativeStereoPacketCapture {
  bool complete = false;
  NativeStereoCaptureReason reason = NativeStereoCaptureReason::complete;
  unsigned buckets = 0, items = 0, meshes = 0, work = 0;
  size_t bytesRead = 0;
  std::vector<NativeStereoPacket> packets;
};
namespace native_stereo_detail {
class Reader {
  bool (*readable_)(const void *, size_t);
  NativeStereoCaptureReason &reason_;
  size_t &bytes_;

public:
  Reader(bool (*readable)(const void *, size_t),
         NativeStereoCaptureReason &reason, size_t &bytes)
      : readable_(readable), reason_(reason), bytes_(bytes) {}
  bool fail(NativeStereoCaptureReason reason) {
    reason_ = reason;
    return false;
  }
  bool span(uintptr_t address, size_t bytes) {
    if (!readable_)
      return fail(NativeStereoCaptureReason::missingReadable);
    if (!address)
      return fail(NativeStereoCaptureReason::missingPointer);
    if (bytes > (std::numeric_limits<uintptr_t>::max)() - address)
      return fail(NativeStereoCaptureReason::addressOverflow);
    if (!readable_(reinterpret_cast<const void *>(address), bytes))
      return fail(NativeStereoCaptureReason::unreadable);
    return true;
  }
  template <class T> bool read(uintptr_t base, size_t offset, T &value) {
    if (!base)
      return fail(NativeStereoCaptureReason::missingPointer);
    if (offset > (std::numeric_limits<uintptr_t>::max)() - base)
      return fail(NativeStereoCaptureReason::addressOverflow);
    const auto address = base + offset;
    if (!span(address, sizeof(T)))
      return false;
    std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(T));
    bytes_ += sizeof(T);
    return true;
  }
};
struct Vector {
  uint64_t begin = 0, end = 0, capacity = 0;
  size_t count = 0;
};
inline bool charge(NativeStereoPacketCapture &out) {
  if (out.work == nativeStereoWorkLimit) {
    out.reason = NativeStereoCaptureReason::workLimit;
    return false;
  }
  ++out.work;
  return true;
}
inline bool vector(Reader &reader, uintptr_t base, size_t offset, Vector &v,
                   NativeStereoPacketCapture &out) {
  if (!charge(out))
    return false;
  if (!reader.read(base, offset, v.begin) ||
      !reader.read(base, offset + 8, v.end) ||
      !reader.read(base, offset + 16, v.capacity))
    return false;
  if ((v.begin | v.end | v.capacity) & 7 || v.end < v.begin ||
      v.capacity < v.end || (!v.begin && (v.end || v.capacity)))
    return reader.fail(NativeStereoCaptureReason::malformedVector);
  v.count = size_t((v.end - v.begin) / 8);
  if ((v.capacity - v.begin) / 8 > nativeStereoWorkLimit)
    return reader.fail(NativeStereoCaptureReason::workLimit);
  // Non-null allocated vectors may be empty after native cleanup. Their
  // headers still consume work, but there is no element span to read.
  if (v.count && !reader.span(uintptr_t(v.begin), size_t(v.end - v.begin)))
    return false;
  return true;
}
inline bool pointer(Reader &reader, const Vector &v, size_t i, uintptr_t &value,
                    NativeStereoPacketCapture &out) {
  if (!charge(out) || !reader.read(uintptr_t(v.begin), i * 8, value))
    return false;
  return value ? true : reader.fail(NativeStereoCaptureReason::missingPointer);
}
} // namespace native_stereo_detail
inline NativeStereoPacketCapture
capture_native_stereo_packets(const void *scene,
                              bool (*readable)(const void *, size_t)) noexcept {
  NativeStereoPacketCapture out;
  try {
    using namespace native_stereo_detail;
    Reader reader(readable, out.reason, out.bytesRead);
    Vector buckets;
    if (!vector(reader, reinterpret_cast<uintptr_t>(scene), 0xdb0, buckets,
                out))
      return out;
    for (size_t b = 0; b < buckets.count; ++b) {
      uintptr_t bucket = 0;
      Vector items;
      if (!pointer(reader, buckets, b, bucket, out))
        return out;
      ++out.buckets;
      if (!vector(reader, bucket, 0x4f0, items, out))
        return out;
      for (size_t i = 0; i < items.count; ++i) {
        uintptr_t item = 0;
        Vector meshes;
        if (!pointer(reader, items, i, item, out))
          return out;
        ++out.items;
        if (!vector(reader, item, 0x490, meshes, out))
          return out;
        for (size_t m = 0; m < meshes.count; ++m) {
          if (out.packets.size() == nativeStereoPacketLimit) {
            out.reason = NativeStereoCaptureReason::packetLimit;
            return out;
          }
          NativeStereoPacket packet;
          uint64_t callback = 0;
          if (!pointer(reader, meshes, m, packet.mesh, out))
            return out;
          ++out.meshes;
          if (!reader.read(packet.mesh, 0, packet.meshFlags) ||
              !reader.read(packet.mesh, 0x80, packet.geometry) ||
              !reader.read(packet.mesh, 0xcc, callback))
            return out;
          packet.meshCallback = callback != 0;
          packet.bucketIndex = unsigned(b);
          packet.itemIndex = unsigned(i);
          packet.meshIndex = unsigned(m);
          if (packet.geometry &&
              !reader.read(packet.geometry, 0, packet.geometryFlags))
            return out;
          out.packets.push_back(packet);
        }
      }
    }
    out.complete = true;
  } catch (...) {
    out.complete = false;
    out.reason = NativeStereoCaptureReason::allocationFailure;
    out.packets.clear();
  }
  return out;
}
} // namespace lmuqv
