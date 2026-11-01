#include "../native_stereo_packets.hpp"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <vector>

// Deterministic allocation failure exercises the real vector growth path.
bool failPacketAllocation = false;
void *operator new(std::size_t bytes) {
  if (failPacketAllocation)
    throw std::bad_alloc();
  if (void *value = std::malloc(bytes ? bytes : 1))
    return value;
  throw std::bad_alloc();
}
void operator delete(void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace lmuqv;
struct Span {
  const void *data;
  size_t size;
};
std::vector<Span> spans;
unsigned checks = 0, callbacks = 0;
bool readable(const void *data, size_t size) {
  ++checks;
  if (!size)
    return false;
  const auto address = reinterpret_cast<uintptr_t>(data);
  for (const auto &span : spans) {
    const auto begin = reinterpret_cast<uintptr_t>(span.data);
    if (address >= begin && address - begin <= span.size &&
        size <= span.size - (address - begin))
      return true;
  }
  return false;
}
void callback() { ++callbacks; }
template <size_t N> struct Memory {
  alignas(8) std::array<unsigned char, N> bytes{};
  Memory() { spans.push_back({bytes.data(), N}); }
  template <class T> void put(size_t offset, T value) {
    assert(offset + sizeof(value) <= N);
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  }
  void *data() { return bytes.data(); }
};
template <class T> void expose(std::vector<T> &values) {
  spans.push_back({values.data(), values.size() * sizeof(T)});
}
template <size_t N>
void vectorAt(Memory<N> &memory, size_t offset,
              const std::vector<uint64_t> &values) {
  const auto begin =
      values.empty() ? 0 : reinterpret_cast<uint64_t>(values.data());
  memory.put(offset, begin);
  memory.put(offset + 8, begin + values.size() * 8);
  memory.put(offset + 16, begin + values.size() * 8);
}
struct Fixture {
  Memory<0xdc8> scene;
  Memory<0x508> bucket;
  Memory<0x4a8> item;
  Memory<0xd4> meshA, meshB;
  Memory<4> geometry;
  std::vector<uint64_t> buckets, items, meshes;
  Fixture()
      : buckets{reinterpret_cast<uint64_t>(bucket.data())},
        items{reinterpret_cast<uint64_t>(item.data())},
        meshes{reinterpret_cast<uint64_t>(meshA.data()),
               reinterpret_cast<uint64_t>(meshB.data()),
               reinterpret_cast<uint64_t>(meshA.data())} {
    expose(buckets);
    expose(items);
    expose(meshes);
    vectorAt(scene, 0xdb0, buckets);
    vectorAt(bucket, 0x4f0, items);
    vectorAt(item, 0x490, meshes);
    meshA.put(0, uint32_t(0x400000));
    meshA.put(0x80, reinterpret_cast<uint64_t>(geometry.data()));
    meshA.put(0xcc, reinterpret_cast<uint64_t>(&callback));
    geometry.put(0, uint32_t(0x4000));
  }
  NativeStereoPacketCapture capture() {
    return capture_native_stereo_packets(scene.data(), readable);
  }
};
} // namespace
int main() {
  using namespace lmuqv;
  Fixture f;
  const auto sceneBefore = f.scene.bytes;
  const auto bucketBefore = f.bucket.bytes;
  const auto itemBefore = f.item.bytes;
  const auto meshBefore = f.meshA.bytes;
  auto captured = f.capture();
  assert(captured.complete && captured.packets.size() == 3 &&
         captured.buckets == 1 && captured.items == 1 && captured.meshes == 3);
  assert(captured.packets[0].mesh == captured.packets[2].mesh &&
         captured.packets[0].mesh != captured.packets[1].mesh);
  assert(captured.packets[0].meshFlags == 0x400000 &&
         captured.packets[0].geometryFlags == 0x4000 &&
         captured.packets[0].meshCallback);
  assert(!captured.packets[1].geometry && !captured.packets[1].meshCallback);
  assert(!callbacks && sceneBefore == f.scene.bytes &&
         bucketBefore == f.bucket.bytes && itemBefore == f.item.bytes &&
         meshBefore == f.meshA.bytes);
  assert(checks > 0);
  // Native callback identities are observed only; none may be invoked.
  const auto good = f.scene.bytes;
  f.scene.put(0xdb0, uint64_t(8));
  f.scene.put(0xdb8, uint64_t(16));
  f.scene.put(0xdc0, uint64_t(16));
  assert(!f.capture().complete);
  f.scene.bytes = good;
  f.scene.put(0xdb8, reinterpret_cast<uint64_t>(f.buckets.data()) + 1);
  assert(!f.capture().complete);
  f.scene.bytes = good;
  f.scene.put(0xdc0, reinterpret_cast<uint64_t>(f.buckets.data()) - 8);
  assert(!f.capture().complete);
  f.scene.bytes = good;
  f.buckets[0] = 0;
  assert(!f.capture().complete);
  f.buckets[0] = reinterpret_cast<uint64_t>(f.bucket.data());
  const auto beforeChecks = checks;
  assert(!capture_native_stereo_packets(
              reinterpret_cast<void *>((std::numeric_limits<uintptr_t>::max)() -
                                       4),
              readable)
              .complete);
  assert(checks ==
         beforeChecks); // Address overflow rejected before the callback.
  assert(!capture_native_stereo_packets(f.scene.data(), nullptr).complete);
  failPacketAllocation = true;
  captured = f.capture();
  failPacketAllocation = false;
  assert(!captured.complete && captured.packets.empty() &&
         captured.reason == NativeStereoCaptureReason::allocationFailure);
  f.meshA.put(0x80, uint64_t(1));
  assert(f.capture().reason == NativeStereoCaptureReason::unreadable);
  f.meshA.put(0x80, reinterpret_cast<uint64_t>(f.geometry.data()));
  f.meshB.put(0x80, uint64_t(1));
  captured = f.capture();
  assert(!captured.complete && captured.packets.size() == 1);
  f.meshB.put(0x80, uint64_t(0));
  spans.pop_back(); // Remove the mesh pointer span.
  assert(!f.capture().complete);
  expose(f.meshes);
  // A null vector is an explicitly empty vector. Spare capacity is validated.
  std::vector<uint64_t> empty;
  vectorAt(f.item, 0x490, empty);
  assert(f.capture().complete && f.capture().packets.empty());
  f.item.put(0x4a0, uint64_t(8));
  assert(!f.capture().complete);
  vectorAt(f.item, 0x490, f.meshes);
  // Cleanup leaves allocated vectors empty by restoring end to begin. The
  // engine's Readable callback rejects zero-byte reads, so do not issue one.
  f.item.put(0x498, reinterpret_cast<uint64_t>(f.meshes.data()));
  assert(f.capture().complete && f.capture().packets.empty());
  vectorAt(f.item, 0x490, f.meshes);
  std::vector<uint64_t> tooMany(4097,
                                reinterpret_cast<uint64_t>(f.meshA.data()));
  expose(tooMany);
  vectorAt(f.item, 0x490, tooMany);
  captured = f.capture();
  assert(!captured.complete && captured.packets.size() == 4096 &&
         captured.reason == NativeStereoCaptureReason::packetLimit);
  std::vector<uint64_t> excessive(16385,
                                  reinterpret_cast<uint64_t>(f.bucket.data()));
  expose(excessive);
  vectorAt(f.scene, 0xdb0, excessive);
  assert(f.capture().reason == NativeStereoCaptureReason::workLimit);
  // Empty nested vectors still consume work; repeated identities cannot bypass
  // the budget.
  vectorAt(f.bucket, 0x4f0, empty);
  std::vector<uint64_t> manyEmpty(9000,
                                  reinterpret_cast<uint64_t>(f.bucket.data()));
  expose(manyEmpty);
  vectorAt(f.scene, 0xdb0, manyEmpty);
  captured = f.capture();
  assert(!captured.complete && captured.work <= 16384 &&
         captured.reason == NativeStereoCaptureReason::workLimit);
  assert(captured.bytesRead <= nativeStereoReadByteLimit);
  // Native order spans bucket and item boundaries, including empty items and
  // repeated mesh identities in separate vectors.
  Memory<0x508> secondBucket;
  Memory<0x4a8> secondItem, emptyItem;
  std::vector<uint64_t> secondMeshes{
      reinterpret_cast<uint64_t>(f.meshB.data()),
      reinterpret_cast<uint64_t>(f.meshA.data())};
  std::vector<uint64_t> secondItems{
      reinterpret_cast<uint64_t>(emptyItem.data()),
      reinterpret_cast<uint64_t>(secondItem.data()),
      reinterpret_cast<uint64_t>(f.item.data())};
  std::vector<uint64_t> twoBuckets{
      reinterpret_cast<uint64_t>(f.bucket.data()),
      reinterpret_cast<uint64_t>(secondBucket.data())};
  expose(secondMeshes);
  expose(secondItems);
  expose(twoBuckets);
  vectorAt(f.scene, 0xdb0, twoBuckets);
  vectorAt(f.bucket, 0x4f0, f.items);
  vectorAt(f.item, 0x490, f.meshes);
  vectorAt(secondBucket, 0x4f0, secondItems);
  vectorAt(secondItem, 0x490, secondMeshes);
  captured = f.capture();
  assert(captured.complete && captured.buckets == 2 && captured.items == 4 &&
         captured.meshes == 8 && captured.packets.size() == 8);
  assert(captured.packets[3].mesh == captured.packets[1].mesh &&
         captured.packets[3].bucketIndex == 1 &&
         captured.packets[3].itemIndex == 1 &&
         captured.packets[5].itemIndex == 2);
  assert(!callbacks);
  std::cout << "Native packet reader passed: order, duplicates, empty vectors, "
               "null geometry, callback observation, immutable inputs and "
               "bounded failures\n";
}
