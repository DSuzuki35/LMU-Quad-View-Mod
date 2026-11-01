#include "../native_stereo_camera.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
using namespace lmuqv;
namespace {
unsigned failures = 0;
void check(bool value, const char *message) {
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
Matrix identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
NativeStereoCameraSnapshot valid() {
  NativeStereoCameraSnapshot s;
  s.complete = true;
  s.reason = "complete";
  s.eye = 0;
  s.transformMode = 1;
  s.cameraMode = 0;
  s.current = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1};
  s.relativeEye = {identity(), identity()};
  s.relativeEye[0][12] = -0.03f;
  s.relativeEye[1][12] = 0.03f;
  s.engineProjection[0] = {2,    0,    0, 0, 0, 3, 0,  0,
                           0.1f, 0.2f, 7, 1, 0, 0, -8, 0};
  s.engineProjection[1] = {4,     0,    0, 0, 0, 5, 0,   0,
                           -0.3f, 0.4f, 9, 1, 0, 0, -10, 0};
  s.engineFarProjection = s.engineProjection;
  s.projection = {2,    0,    0,       0, 0, 3, 0,    0,
                  0.1f, 0.2f, -0.001f, 1, 0, 0, 0.1f, 0};
  s.farProjection = s.projection;
  s.farProjection[10] = -0.0001f;
  s.farProjection[14] = 0.01f;
  s.derived = multiply(s.current, s.projection);
  return s;
}
std::vector<NativeStereoCameraObservation> pair() {
  auto l = valid(), r = l;
  r.eye = 1;
  r.current[12] = 10.06f;
  r.projection[0] = 4;
  r.projection[5] = 5;
  r.projection[8] = -0.3f;
  r.projection[9] = 0.4f;
  r.farProjection = r.projection;
  r.farProjection[10] = -0.0001f;
  r.farProjection[14] = 0.01f;
  r.derived = {0,     5,    0,       0, -4,     0,   0,     0,
               -0.3f, 0.4f, -0.001f, 1, 31.24f, 112, 0.07f, 30};
  return {{7, 0, 1, 2, true, l}, {7, 1, 1, 2, true, r}};
}
void refusal(std::vector<NativeStereoCameraObservation> p, const char *reason) {
  const auto c = compare_native_stereo_cameras(p.data(), p.size(), 0);
  check(!c.available && std::string(c.reason).find(reason) != std::string::npos,
        reason);
}
struct Buffers {
  std::array<unsigned char, 0x5d0> view{};
  std::array<unsigned char, 0x418> state{};
  int32_t mode = 0;
  uintptr_t current = 0;
  template <class T, size_t N>
  static void put(std::array<unsigned char, N> &b, size_t offset, const T &v) {
    std::memcpy(b.data() + offset, &v, sizeof(v));
  }
  Buffers() {
    const auto s = valid();
    current = reinterpret_cast<uintptr_t>(view.data());
    put(view, 0, s.flags);
    put(view, 0x5cc, s.eye);
    put(view, 0x21c, s.transformMode);
    put(view, 0x1dc, s.current);
    put(view, 0x360, s.projection);
    put(view, 0x58c, s.farProjection);
    put(view, 0x3a0, s.derived);
    put(view, 0xc4, s.translation);
    put(view, 0xd0, s.euler);
    for (unsigned i = 0; i < 2; ++i) {
      put(state, 0x70 + 0x6c * i, s.relativeEye[i]);
      put(state, 0x148 + 0x6c * i, s.engineProjection[i]);
      put(state, 0x220 + 0x6c * i, s.engineFarProjection[i]);
      put(state, 0x3e0 + 24 * i, s.scalars[i]);
    }
  }
};
Buffers *buffers = nullptr;
unsigned reads = 0;
uintptr_t blocked = 0;
bool readable(const void *p, size_t n) {
  ++reads;
  const auto a = reinterpret_cast<uintptr_t>(p),
             b = reinterpret_cast<uintptr_t>(buffers);
  return a != blocked && a >= b && a - b <= sizeof(Buffers) &&
         n <= sizeof(Buffers) - (a - b);
}
NativeStereoCameraSnapshot capture(Buffers &b) {
  buffers = &b;
  return capture_native_stereo_camera(b.view.data(), b.state.data(), &b.mode,
                                      &b.current, readable);
}
} // namespace
int main() {
  auto p = pair();
  auto c = compare_native_stereo_cameras(p.data(), p.size(), 0);
  check(c.available, "analytic translated/rotated rigid pair available");
  check(std::abs(c.current[12] - 10.06f) < 1e-5f && c.current[13] == 20,
        "row-major C_left E_right inverse(E_left) order");
  check(c.projection[0] == 4 && c.projection[5] == 5 &&
            c.projection[8] == -0.3f && c.projection[9] == 0.4f,
        "asymmetric right FOV coefficients");
  check(c.projection[10] == -0.001f && c.projection[11] == 1 &&
            c.projection[14] == 0.1f && c.farProjection[14] == 0.01f,
        "left depth convention retained");
  check(c.currentError < 1e-5 && c.projectionError == 0 &&
            c.farProjectionError == 0 && c.derivedError < 1e-5,
        "four independently analytic errors");
  check(std::abs(c.derived[12] - 31.24f) < 1e-5 && c.derived[13] == 112 &&
            c.derived[15] == 30,
        "derived is current times projection");
  p = pair();
  for (auto &o : p) {
    o.camera.relativeEye[0] = {0, 1, 0, 0, -1,     0, 0, 0,
                               0, 0, 1, 0, -0.03f, 0, 0, 1};
    o.camera.relativeEye[1] = o.camera.relativeEye[0];
    o.camera.relativeEye[1][12] = 0.03f;
  }
  p[1].camera.current[12] = 10;
  p[1].camera.current[13] = 19.94f;
  c = compare_native_stereo_cameras(p.data(), p.size(), 0);
  check(c.available && c.currentError < 1e-5,
        "rotated eye rigid inverse includes translation");
  p = pair();
  p.pop_back();
  refusal(p, "missing right");
  p = pair();
  p.erase(p.begin());
  refusal(p, "missing left");
  p = pair();
  p[1].generation++;
  refusal(p, "missing right");
  p = pair();
  p[1].slot += 4;
  refusal(p, "missing right");
  p = pair();
  p[1].slot = 3;
  refusal(p, "missing right");
  p = pair();
  p[0].slot = 2;
  p[1].slot = 3;
  refusal(p, "unsupported focus projection input");
  p = pair();
  p[1].view++;
  refusal(p, "identity");
  p = pair();
  p[1].scene++;
  refusal(p, "identity");
  p = pair();
  p[0].scene = p[1].scene = 0;
  refusal(p, "incomplete identity");
  p = pair();
  p[1].camera.current[12] += 5;
  p[1].camera.projection[0] += 0.5f;
  p[1].camera.farProjection[14] += 2;
  p[1].camera.derived[15] += 7;
  c = compare_native_stereo_cameras(p.data(), p.size(), 0);
  check(c.available && std::abs(c.currentError - 5) < 1e-5 &&
            c.projectionError == 0.5 && c.farProjectionError > 1.99 &&
            c.derivedError == 7,
        "reports errors without an invented acceptance threshold");
  p = pair();
  p.push_back(p[0]);
  refusal(p, "duplicate");
  p = pair();
  p.push_back(p[1]);
  refusal(p, "duplicate");
  p = pair();
  p[1].ended = false;
  refusal(p, "incomplete");
  p = pair();
  p.push_back(p[0]);
  p.back().scene = 99;
  p.back().ended = false;
  refusal(p, "capture incomplete");
  p = pair();
  p[1].camera.complete = false;
  refusal(p, "incomplete");
  p = pair();
  p[1].camera.eye = 0;
  refusal(p, "eye");
  p = pair();
  p[1].camera.scalars[0][5] = 1;
  refusal(p, "payload");
  p = pair();
  p[1].camera.engineFarProjection[1][0] += 0.1f;
  refusal(p, "payload");
  for (unsigned field = 0; field < 10; ++field) {
    p = pair();
    for (auto &o : p) {
      auto &s = o.camera;
      switch (field) {
      case 0:
        s.cameraMode = 1;
        break;
      case 1:
        s.transformMode = 0;
        break;
      case 2:
        s.flags = 0x01000000;
        break;
      case 3:
        s.flags = 0x80000000;
        break;
      case 4:
        s.flags = 0x40000000;
        break;
      case 5:
        s.translation[1] = 0.1f;
        break;
      case 6:
        s.euler[2] = 0.1f;
        break;
      case 7:
        s.relativeEye[1][0] = 0;
        break;
      case 8:
        s.scalars[1][0] = 0.01f;
        break;
      case 9:
        s.current[0] = 2;
        break;
      }
    }
    refusal(p, "unsupported");
  }
  p = pair();
  for (auto &o : p)
    o.camera.relativeEye[1] = {0, 1, 0, 0, -1,    0, 0, 0,
                               0, 0, 1, 0, 0.03f, 0, 0, 1};
  refusal(p, "unsupported");
  p = pair();
  for (auto &o : p)
    o.camera.relativeEye[0][0] = -1;
  refusal(p, "unsupported");
  p = pair();
  for (auto &o : p)
    o.camera.current[3] = 1;
  refusal(p, "unsupported");
  p = pair();
  p[1].camera.derived[3] = std::numeric_limits<float>::infinity();
  refusal(p, "non-finite");
  Buffers b;
  auto s = capture(b);
  check(s.complete && s.current[12] == 10 && s.relativeEye[1][12] == 0.03f &&
            reads > 0 && s.bytesRead == nativeStereoCameraReadByteLimit,
        "bounded source numeric capture");
  Buffers::put(b.view, 0x1dc, identity());
  check(s.current[12] == 10, "snapshot owns copied values");
  blocked = reinterpret_cast<uintptr_t>(b.state.data() + 0x70);
  s = capture(b);
  check(!s.complete &&
            std::string(s.reason).find("unreadable") != std::string::npos,
        "unreadable span refused before copy");
  blocked = 0;
  s = capture_native_stereo_camera(nullptr, b.state.data(), &b.mode, &b.current,
                                   readable);
  check(!s.complete, "null view refusal");
  s = capture_native_stereo_camera(b.view.data(), nullptr, &b.mode, &b.current,
                                   readable);
  check(!s.complete, "null camera refusal");
  s = capture_native_stereo_camera(b.view.data(), b.state.data(), nullptr,
                                   &b.current, readable);
  check(!s.complete, "null mode refusal");
  s = capture_native_stereo_camera(b.view.data(), b.state.data(), &b.mode,
                                   nullptr, readable);
  check(!s.complete, "null current-view source refusal");
  s = capture_native_stereo_camera(b.view.data(), b.state.data(), &b.mode,
                                   &b.current, nullptr);
  check(!s.complete, "missing readable refusal");
  s = capture_native_stereo_camera(
      reinterpret_cast<void *>((std::numeric_limits<uintptr_t>::max)() - 8),
      b.state.data(), &b.mode, &b.current, readable);
  check(!s.complete, "address overflow refusal");
  b.current = 0;
  s = capture(b);
  check(!s.complete &&
            std::string(s.reason).find("main view") != std::string::npos,
        "non-main current-view refusal");
  b.current = reinterpret_cast<uintptr_t>(b.view.data());
  Buffers::put(b.view, 0x4b0, uint64_t(1));
  s = capture(b);
  check(!s.complete && std::string(s.reason).find("child") != std::string::npos,
        "child view refusal");
  Buffers::put(b.view, 0x4b0, uint64_t(0));
  const auto inf = std::numeric_limits<float>::infinity();
  for (size_t offset : {size_t(0x1dc), size_t(0x360), size_t(0x58c),
                        size_t(0x3a0), size_t(0xc4), size_t(0xd0)}) {
    b = Buffers();
    b.current = reinterpret_cast<uintptr_t>(b.view.data());
    Buffers::put(b.view, offset, inf);
    s = capture(b);
    check(!s.complete &&
              std::string(s.reason).find("non-finite") != std::string::npos,
          "all view numeric blocks finite");
  }
  for (size_t offset :
       {size_t(0x70), size_t(0xdc), size_t(0x148), size_t(0x1b4), size_t(0x220),
        size_t(0x28c), size_t(0x3e0), size_t(0x3f8)}) {
    b = Buffers();
    b.current = reinterpret_cast<uintptr_t>(b.view.data());
    Buffers::put(b.state, offset, inf);
    s = capture(b);
    check(!s.complete &&
              std::string(s.reason).find("non-finite") != std::string::npos,
          "all camera numeric blocks finite");
  }
  std::cout << "Native stereo camera tests: " << failures << " failures\n";
  return failures ? 1 : 0;
}
