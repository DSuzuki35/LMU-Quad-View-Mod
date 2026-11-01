#include <cassert>
#include <iostream>
#if __has_include("../native_stereo_classify_capture.hpp")
#include "../native_stereo_classify_capture.hpp"
#include <array>
#include <stdexcept>
using namespace lmuqv;
namespace {
struct Fixture {
  std::array<unsigned char, 0x138> candidate{};
  std::array<unsigned char, 0xf0> owner{};
  std::array<unsigned char, 0x4b8> view{};
  NativeStereoClassifyFloat3 position{3, 4, 0};
  uintptr_t main = 0;
  template <class T, size_t N> static void put(std::array<unsigned char, N> &a, size_t o, T v) {
    std::memcpy(a.data() + o, &v, sizeof(v));
  }
  Fixture() {
    main = reinterpret_cast<uintptr_t>(view.data());
    put(candidate, 0, uint32_t(0x70020));
    put(candidate, 0xdc, 2.f);
    put(candidate, 0xfc, 0.f);
    put(candidate, 0x100, 10.f);
    put(candidate, 0x104, int32_t(4));
    put(candidate, 0x110, reinterpret_cast<uintptr_t>(&position));
    put(candidate, 0x130, reinterpret_cast<uintptr_t>(owner.data()));
    put(owner, 0, uint32_t(0x80000000));
    put(owner, 0xec, 1.f);
    put(view, 0x114, 1.f);
    for (unsigned i = 0; i < 4; ++i)
      put(view, 0x178 + i * 16, NativeStereoClassifyPlane{0, 0, 0, -3});
  }
  NativeStereoClassifyCaptureArgs args() {
    return {candidate.data(),
            view.data(),
            reinterpret_cast<const float *>(&position),
            false,
            0.f,
            &main};
  }
} fixture;
unsigned reads = 0;
size_t charged = 0;
bool throwing = false, unreadable = false;
bool readable(const void *p, size_t n) {
  ++reads;
  charged += n;
  if (throwing)
    throw std::runtime_error("reader");
  if (unreadable)
    return false;
  const auto a = reinterpret_cast<uintptr_t>(p), b = reinterpret_cast<uintptr_t>(&fixture);
  return a >= b && a - b <= sizeof(fixture) && n <= sizeof(fixture) - (a - b);
}
NativeStereoClassifySnapshot snapshot(NativeStereoClassifyCaptureArgs args, size_t &bytes) {
  return capture_native_stereo_classify(args, readable, bytes);
}
} // namespace
int main() {
  NativeStereoFpGuard caller;
  _mm_setcsr(0x1f80);
  for (int planes = 0; planes <= 4; ++planes) {
    Fixture::put(fixture.candidate, 0x104, int32_t(planes));
    size_t bytes = 0;
    auto pre = snapshot(fixture.args(), bytes);
    assert(pre.complete && pre.input.planeCount == planes && pre.input.position.x == 3 &&
           pre.input.position.y == 4);
    assert(pre.input.candidateFlags == 0x70020 && pre.input.ownerFlags == 0x80000000 &&
           pre.input.radius == 2 && pre.input.farCoefficient == 10);
    Fixture::put(fixture.candidate, 0, uint32_t(0x20));
    auto post = snapshot(fixture.args(), bytes);
    auto comparison = compare_native_stereo_classify(pre, post, 0x1f81, 0x1fa0, 0x20);
    assert(comparison.available && !comparison.returnedMismatch && !comparison.storedMismatch &&
           bytes == size_t(176 + 32 * planes) && charged >= bytes);
    assert(compare_native_stereo_classify(pre, post, 0x1f80, 0x1f80, 0x21).returnedMismatch);
    Fixture::put(fixture.candidate, 0, uint32_t(0x20020));
    bytes = 0;
    post = snapshot(fixture.args(), bytes);
    assert(compare_native_stereo_classify(pre, post, 0x1f80, 0x1f80, 0x20).storedMismatch);
    assert(!compare_native_stereo_classify(pre, post, 0x9f80, 0x1f80, 0x20).available);
    Fixture::put(fixture.candidate, 0, uint32_t(0x70020));
  }
  size_t bytes = 0;
  auto pre = snapshot(fixture.args(), bytes);
  assert(pre.complete);
  fixture.position.x = 9;
  assert(pre.input.position.x == 3);
  auto post = snapshot(fixture.args(), bytes);
  assert(!compare_native_stereo_classify(pre, post, 0x1f80, 0x1f80, 0x20).available);
  fixture.position.x = 3;
  bytes = 0;
  pre = snapshot(fixture.args(), bytes);
  Fixture::put(fixture.candidate, 0, uint32_t(0x70021));
  post = snapshot(fixture.args(), bytes);
  assert(!compare_native_stereo_classify(pre, post, 0x1f80, 0x1f80, 0x20).available);
  Fixture::put(fixture.candidate, 0, uint32_t(0x70020));
  auto bad = fixture.args();
  bad.skipDistanceBounds = true;
  bytes = 0;
  assert(!snapshot(bad, bytes).complete);
  for (int count : {-1, 5}) {
    Fixture::put(fixture.candidate, 0x104, int32_t(count));
    bytes = 0;
    assert(!snapshot(fixture.args(), bytes).complete);
  }
  Fixture::put(fixture.candidate, 0x104, int32_t(4));
  bad = fixture.args();
  bad.position = nullptr;
  bytes = 0;
  assert(!snapshot(bad, bytes).complete);
  bad = fixture.args();
  bad.view = nullptr;
  bytes = 0;
  assert(!snapshot(bad, bytes).complete);
  bad = fixture.args();
  bad.candidate = reinterpret_cast<void *>(UINTPTR_MAX - 1);
  bytes = 0;
  assert(!snapshot(bad, bytes).complete);
  bad = fixture.args();
  bad.mainViewPointer = nullptr;
  bytes = 0;
  assert(!snapshot(bad, bytes).complete);
  unreadable = true;
  bytes = 0;
  assert(!snapshot(fixture.args(), bytes).complete);
  unreadable = false;
  throwing = true;
  bytes = 0;
  assert(!snapshot(fixture.args(), bytes).complete);
  throwing = false;
  bytes = 384;
  reads = 0;
  assert(!snapshot(fixture.args(), bytes).complete && reads == 0 && bytes == 384);
  // Wrong main view and a parented view refuse before candidate inspection.
  bad = fixture.args();
  bad.view = fixture.owner.data();
  bytes = 0;
  reads = 0;
  assert(!snapshot(bad, bytes).complete && reads == 1 && bytes == 8);
  Fixture::put(fixture.view, 0x4b0, uintptr_t(1));
  bytes = 0;
  reads = 0;
  assert(!snapshot(fixture.args(), bytes).complete && reads == 2 && bytes == 16);
  Fixture::put(fixture.view, 0x4b0, uintptr_t(0));
  bytes = 0;
  pre = snapshot(fixture.args(), bytes);
  Fixture::put(fixture.owner, 0xec, 2.f);
  post = snapshot(fixture.args(), bytes);
  assert(!compare_native_stereo_classify(pre, post, 0x1f80, 0x1f80, 0x20).available);
  Fixture::put(fixture.owner, 0xec, 1.f);
  // A signalling NaN is copied without arithmetic and refuses under the
  // export comparison guard without leaking its invalid-operation flag.
  Fixture::put(fixture.candidate, 0xdc, uint32_t(0x7f800001));
  bytes = 0;
  _mm_setcsr(0x1f82);
  pre = snapshot(fixture.args(), bytes);
  assert(_mm_getcsr() == 0x1f82);
  post = snapshot(fixture.args(), bytes);
  assert(!compare_native_stereo_classify(pre, post, 0x1f80, 0x1f80, 0x20).available &&
         _mm_getcsr() == 0x1f82);
  Fixture::put(fixture.candidate, 0xdc, 2.f);
  // Use the same forwarding wrapper with a native analytic stub. The stub
  // never calls the numeric model; 3-4-5 distance with far=10 is inside.
  NativeStereoClassifySnapshot nativePre, nativePost;
  unsigned analyticCalls = 0;
  bytes = 0;
  const auto returned = invoke_native_stereo_classify(
      [&](uint32_t) {
        nativePre = snapshot(fixture.args(), bytes);
        return 1;
      },
      [&] {
        ++analyticCalls;
        Fixture::put(fixture.candidate, 0, uint32_t(0x20));
        return uint32_t(0x20);
      },
      [&](int, uint32_t, uint32_t) { nativePost = snapshot(fixture.args(), bytes); });
  const auto analytic =
      compare_native_stereo_classify(nativePre, nativePost, 0x1f80, 0x1f80, returned);
  assert(analyticCalls == 1 && analytic.available && !analytic.returnedMismatch &&
         !analytic.storedMismatch);
  for (bool failBegin : {false, true})
    for (bool failEnd : {false, true}) {
      unsigned calls = 0;
      _mm_setcsr(0x1f81);
      auto result = invoke_native_stereo_classify(
          [&](uint32_t entry) {
            assert(entry == 0x1f81);
            _mm_setcsr(0x9f80);
            if (failBegin)
              throw 1;
            return 7;
          },
          [&] {
            ++calls;
            assert(_mm_getcsr() == 0x1f81);
            _mm_setcsr(0x3fa0);
            return uint32_t(0xfeed1234);
          },
          [&](int, uint32_t value, uint32_t postCsr) {
            assert(value == 0xfeed1234 && postCsr == 0x3fa0);
            _mm_setcsr(0x1f80);
            if (failEnd)
              throw 2;
          });
      assert(result == 0xfeed1234 && calls == 1 && _mm_getcsr() == 0x3fa0);
    }
  _mm_setcsr(0x9fc1);
  {
    NativeStereoFpGuard guard(true);
    assert(_mm_getcsr() == 0x1f80);
    std::feraiseexcept(FE_INVALID);
  }
  assert(_mm_getcsr() == 0x9fc1);
  std::fesetround(FE_DOWNWARD);
  const auto round = std::fegetround();
  {
    NativeStereoFpGuard guard(true);
    assert(std::fegetround() == FE_TONEAREST);
  }
  assert(std::fegetround() == round);
  std::cout << "Native classifier copied capture, comparison and original-once FP wrapper passed\n";
}
#else
int main() {
  std::cerr << "missing classifier capture helper\n";
  assert(false && "copied classifier snapshot is not implemented");
}
#endif
