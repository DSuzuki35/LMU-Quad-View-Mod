#include "../native_stereo_source.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace lmuqv;
namespace {
std::array<unsigned char, 0x178> geometry{};
unsigned checks = 0, failAt = 0, throwAt = 0;
size_t requested = 0;
bool readable(const void *pointer, size_t bytes) {
  ++checks;
  requested += bytes;
  if (checks == throwAt)
    throw std::runtime_error("reader failure");
  if (checks == failAt)
    return false;
  const auto p = reinterpret_cast<uintptr_t>(pointer),
             begin = reinterpret_cast<uintptr_t>(geometry.data());
  // Only the four scalar fields may be requested: never mesh/array contents.
  const bool field = (p == begin + 0x168 && bytes == 4) ||
                     (p == begin + 0x170 && bytes == 8) ||
                     (p == begin + 0xf8 && bytes == 4) ||
                     (p == begin + 0x138 && bytes == 8);
  assert(field);
  return true;
}
template <class T> void put(size_t offset, T value) {
  std::memcpy(geometry.data() + offset, &value, sizeof(value));
}
void reset() {
  geometry.fill(0);
  checks = failAt = throwAt = 0;
  requested = 0;
  put(0x168, int32_t(3));
  put(0x170, uint64_t(0x10000));
  put(0xf8, 0.12345679f);
  put(0x138, uint64_t(0x87654321));
}
NativeStereoSourceSnapshot capture(uintptr_t mesh = 0x100e0) {
  return capture_native_stereo_source(mesh,
      reinterpret_cast<uintptr_t>(geometry.data()), readable);
}
void failed(const NativeStereoSourceSnapshot &s, const char *reason) {
  assert(!s.complete && std::string(s.reason) == reason);
  // A partial read never publishes a partially populated value payload.
  assert(s.arrayIdentity == 0 && s.count == 0 && s.index == 0 &&
         s.sortDistance == 0 && !s.lazyStatePresent);
  assert(s.bytesRead <= 24 && requested <= 24);
}
}
int main() {
  // Removing exact containment, signed-count validation, or any bounded read
  // guard must fail these cases. Mesh addresses are deliberately synthetic.
  for (const auto mesh : {uintptr_t(0x10000), uintptr_t(0x100e0), uintptr_t(0x101c0)}) {
    reset();
    const auto s = capture(mesh);
    assert(s.complete && std::string(s.reason) == "complete");
    assert(s.arrayIdentity == 0x10000 && s.count == 3 &&
           s.index == (mesh == 0x10000 ? 0u : mesh == 0x100e0 ? 1u : 2u));
    assert(s.sortDistance == 0.12345679f && s.lazyStatePresent);
    assert(s.bytesRead == 24 && requested == 24 && checks == 4);
    geometry.fill(0xff);
    assert(s.count == 3 && s.arrayIdentity == 0x10000 &&
           s.sortDistance == 0.12345679f && s.lazyStatePresent);
  }
  reset(); put(0x138, uint64_t(0));
  assert(capture().complete && !capture().lazyStatePresent);
  reset(); put(0x168, int32_t(16384));
  const auto last = capture(0x38ff20);
  assert(last.complete && last.index == 16383);
  reset(); failed(capture_native_stereo_source(0,
      reinterpret_cast<uintptr_t>(geometry.data()), readable), "missing required pointer");
  assert(checks == 0);
  reset(); failed(capture_native_stereo_source(0x10000, 0, readable), "missing required pointer");
  assert(checks == 0);
  reset(); failed(capture_native_stereo_source(0x10000,
      reinterpret_cast<uintptr_t>(geometry.data()), nullptr), "missing readable callback");
  assert(checks == 0);
  for (unsigned field = 1; field <= 4; ++field) {
    reset(); failAt = field;
    const auto s = capture(); failed(s, "unreadable native span");
    assert(checks == field);
    reset(); throwAt = field;
    failed(capture(), "diagnostic allocation or reader exception");
    assert(checks == field);
  }
  for (const int32_t count : {0, -1, (std::numeric_limits<int32_t>::min)(), 16385}) {
    reset(); put(0x168, count); failed(capture(), "invalid source record count");
  }
  reset(); put(0x170, uint64_t(0)); failed(capture(), "missing source array");
  reset(); put(0x170, uint64_t((std::numeric_limits<uintptr_t>::max)() - 0x100));
  failed(capture(), "address overflow");
  reset(); failed(capture_native_stereo_source(0x10000,
      (std::numeric_limits<uintptr_t>::max)() - 0x100, readable), "address overflow");
  assert(checks == 0);
  for (const auto mesh : {uintptr_t(0xffff), uintptr_t(0x102a0), uintptr_t(0x102a1)}) {
    reset(); failed(capture(mesh), "mesh outside source array");
  }
  for (const auto mesh : {uintptr_t(0x10001), uintptr_t(0x100df), uintptr_t(0x100e1)}) {
    reset(); failed(capture(mesh), "mesh is not a source record boundary");
  }
  for (const float distance : {(std::numeric_limits<float>::quiet_NaN)(),
                              (std::numeric_limits<float>::infinity)(),
                              -(std::numeric_limits<float>::infinity)()}) {
    reset(); put(0xf8, distance); failed(capture(), "non-finite sort distance");
  }
  std::cout << "Native source containment: scalar ownership, exact bounds, failures and 24-byte limit passed\n";
}
