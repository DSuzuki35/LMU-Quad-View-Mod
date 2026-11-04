#pragma once
#include "native_stereo_packets.hpp"
#include "quad_math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace lmuqv {
inline constexpr size_t nativeStereoCameraReadByteLimit = 744;
// Owned numeric observations only. No native object may be invoked through
// these.
struct NativeStereoCameraSnapshot {
  bool complete = false;
  const char *reason = "incomplete snapshot";
  size_t bytesRead = 0;
  uint32_t flags = 0;
  int32_t eye = -1, transformMode = 0, cameraMode = -1;
  Matrix current{}, projection{}, farProjection{}, derived{};
  std::array<float, 3> translation{}, euler{};
  std::array<Matrix, 2> relativeEye{}, engineProjection{},
      engineFarProjection{};
  std::array<std::array<float, 6>, 2> scalars{};
};
struct NativeStereoCameraObservation {
  uint64_t generation = 0;
  unsigned slot = 0;
  uint32_t scene = 0, view = 0;
  bool ended = false;
  NativeStereoCameraSnapshot camera;
};
struct NativeStereoCameraComparison {
  bool available = false;
  const char *reason = "comparison unavailable";
  Matrix current{}, projection{}, farProjection{}, derived{};
  double currentError = 0, projectionError = 0, farProjectionError = 0,
         derivedError = 0;
};
namespace native_camera_detail {
template <class T> bool finite(const T &numbers) {
  for (const auto value : numbers)
    if (!std::isfinite(value))
      return false;
  return true;
}
inline bool finite(const NativeStereoCameraSnapshot &s) {
  if (!finite(s.current) || !finite(s.projection) || !finite(s.farProjection) ||
      !finite(s.derived) || !finite(s.translation) || !finite(s.euler))
    return false;
  for (unsigned eye = 0; eye < 2; ++eye)
    if (!finite(s.relativeEye[eye]) || !finite(s.engineProjection[eye]) ||
        !finite(s.engineFarProjection[eye]) || !finite(s.scalars[eye]))
      return false;
  return true;
}
inline bool rigid(const Matrix &m) {
  constexpr double tolerance = 1e-4;
  if (!finite(m) || std::abs(m[3]) > tolerance || std::abs(m[7]) > tolerance ||
      std::abs(m[11]) > tolerance || std::abs(double(m[15]) - 1) > tolerance)
    return false;
  for (unsigned row = 0; row < 3; ++row)
    for (unsigned other = 0; other < 3; ++other) {
      double dot = 0;
      for (unsigned col = 0; col < 3; ++col)
        dot += double(m[row * 4 + col]) * m[other * 4 + col];
      if (std::abs(dot - (row == other ? 1.0 : 0.0)) > tolerance)
        return false;
    }
  const double determinant =
      double(m[0]) * (double(m[5]) * m[10] - double(m[6]) * m[9]) -
      double(m[1]) * (double(m[4]) * m[10] - double(m[6]) * m[8]) +
      double(m[2]) * (double(m[4]) * m[9] - double(m[5]) * m[8]);
  return std::abs(determinant - 1) <= tolerance;
}
inline Matrix rigid_inverse(const Matrix &m) {
  Matrix inverse{};
  inverse[15] = 1;
  for (unsigned row = 0; row < 3; ++row)
    for (unsigned col = 0; col < 3; ++col)
      inverse[row * 4 + col] = m[col * 4 + row];
  for (unsigned col = 0; col < 3; ++col)
    for (unsigned k = 0; k < 3; ++k)
      inverse[12 + col] -= m[12 + k] * inverse[k * 4 + col];
  return inverse;
}
inline bool supported(const NativeStereoCameraSnapshot &s) {
  if (s.cameraMode != 0 || s.transformMode != 1 || (s.flags & 0xc1000000u) ||
      !rigid(s.current) || !rigid(s.relativeEye[0]) || !rigid(s.relativeEye[1]))
    return false;
  for (unsigned i = 0; i < 3; ++i) {
    if (s.translation[i] != 0 || s.euler[i] != 0 ||
        std::abs(double(s.scalars[0][i]) - s.scalars[1][i]) > 1e-6)
      return false;
    for (unsigned j = 0; j < 3; ++j)
      if (std::abs(double(s.relativeEye[0][i * 4 + j]) -
                   s.relativeEye[1][i * 4 + j]) > 1e-6)
        return false;
  }
  return true;
}
inline bool same_payload(const NativeStereoCameraSnapshot &a,
                         const NativeStereoCameraSnapshot &b) {
  return a.relativeEye == b.relativeEye &&
         a.engineProjection == b.engineProjection &&
         a.engineFarProjection == b.engineFarProjection &&
         a.scalars == b.scalars && a.cameraMode == b.cameraMode;
}
inline double maximum_error(const Matrix &predicted, const Matrix &observed) {
  double error = 0;
  for (unsigned i = 0; i < 16; ++i)
    error = (std::max)(error, std::abs(double(predicted[i]) - observed[i]));
  return error;
}
} // namespace native_camera_detail
inline NativeStereoCameraSnapshot
capture_native_stereo_camera(const void *view, const void *camera,
                             const void *cameraMode, const void *currentView,
                             bool (*readable)(const void *, size_t)) noexcept {
  NativeStereoCameraSnapshot out;
  NativeStereoCaptureReason readReason = NativeStereoCaptureReason::complete;
  try {
    native_stereo_detail::Reader reader(readable, readReason, out.bytesRead);
    const auto v = reinterpret_cast<uintptr_t>(view),
               c = reinterpret_cast<uintptr_t>(camera);
    uintptr_t mainView = 0;
    uint64_t parent = 0;
    if (!reader.read(reinterpret_cast<uintptr_t>(currentView), 0, mainView)) {
      out.reason = native_stereo_capture_reason(readReason);
      return out;
    }
    if (!v) {
      out.reason = "missing required pointer";
      return out;
    }
    if (mainView != v) {
      out.reason = "unsupported non-main view";
      return out;
    }
    const bool copied = reader.read(v, 0x4b0, parent) &&
                        reader.read(v, 0, out.flags) &&
                        reader.read(v, 0x5cc, out.eye) &&
                        reader.read(v, 0x21c, out.transformMode) &&
                        reader.read(reinterpret_cast<uintptr_t>(cameraMode), 0,
                                    out.cameraMode) &&
                        reader.read(v, 0x1dc, out.current) &&
                        reader.read(v, 0x360, out.projection) &&
                        reader.read(v, 0x58c, out.farProjection) &&
                        reader.read(v, 0x3a0, out.derived) &&
                        reader.read(v, 0xc4, out.translation) &&
                        reader.read(v, 0xd0, out.euler);
    if (!copied) {
      out.reason = native_stereo_capture_reason(readReason);
      return out;
    }
    if (parent) {
      out.reason = "unsupported child view";
      return out;
    }
    for (unsigned eye = 0; eye < 2; ++eye) {
      // TransformRecord has a four-byte metadata prefix and a 0x6c stride.
      if (!reader.read(c, 0x70 + 0x6c * eye, out.relativeEye[eye]) ||
          !reader.read(c, 0x148 + 0x6c * eye, out.engineProjection[eye]) ||
          !reader.read(c, 0x220 + 0x6c * eye, out.engineFarProjection[eye]) ||
          !reader.read(c, 0x3e0 + 24 * eye, out.scalars[eye])) {
        out.reason = native_stereo_capture_reason(readReason);
        return out;
      }
    }
    if (!native_camera_detail::finite(out)) {
      out.reason = "non-finite camera numbers";
      return out;
    }
    out.complete = true;
    out.reason = "complete";
  } catch (...) {
    out.reason = "diagnostic allocation or reader exception";
  }
  return out;
}
inline NativeStereoCameraComparison
compare_native_stereo_cameras(const NativeStereoCameraObservation *observations,
                              size_t count, size_t anchor,
                              bool completeCapture = true) {
  NativeStereoCameraComparison out;
  const auto refuse = [&](const char *reason) {
    out.reason = reason;
    return out;
  };
  if (!observations || anchor >= count)
    return refuse("incomplete snapshot");
  if (!completeCapture)
    return refuse(
        "capture incomplete: dropped, aborted or unfinished native scopes");
  for (size_t i = 0; i < count; ++i)
    if (!observations[i].ended)
      return refuse("capture incomplete: unfinished native scope");
  const auto &key = observations[anchor];
  if (!key.scene || !key.view)
    return refuse("incomplete identity");
  if ((key.slot % 4) >= 2)
    return refuse("unsupported focus projection input");
  const NativeStereoCameraObservation *eyes[2]{};
  unsigned matches[2]{};
  bool differentIdentity = false;
  for (size_t i = 0; i < count; ++i) {
    const auto &candidate = observations[i];
    if (candidate.generation != key.generation ||
        candidate.slot / 4 != key.slot / 4 ||
        (candidate.slot % 4) / 2 != (key.slot % 4) / 2)
      continue;
    if (candidate.scene != key.scene || candidate.view != key.view) {
      differentIdentity = true;
      continue;
    }
    const auto eye = candidate.slot % 2;
    ++matches[eye];
    eyes[eye] = &candidate;
  }
  if (matches[0] > 1 || matches[1] > 1)
    return refuse("duplicate same-eye scopes");
  if (!matches[0] || !matches[1]) {
    if (differentIdentity)
      return refuse("mismatched scene/view identity");
    return refuse(!matches[0] ? "missing left snapshot"
                              : "missing right snapshot");
  }
  const auto &left = eyes[0]->camera;
  const auto &right = eyes[1]->camera;
  if (!eyes[0]->ended || !eyes[1]->ended || !left.complete || !right.complete)
    return refuse("incomplete snapshot");
  if (left.eye != 0 || right.eye != 1)
    return refuse("unsupported eye index for pass");
  if (!native_camera_detail::finite(left) ||
      !native_camera_detail::finite(right))
    return refuse("non-finite camera numbers");
  if (!native_camera_detail::supported(left) ||
      !native_camera_detail::supported(right))
    return refuse("unsupported camera path");
  if (!native_camera_detail::same_payload(left, right))
    return refuse("mismatched camera payload");
  // Row-vector convention: preserve this order, including translated inverse.
  out.current =
      multiply(multiply(left.current, left.relativeEye[1]),
               native_camera_detail::rigid_inverse(left.relativeEye[0]));
  out.projection = replace_fov(left.projection, left.engineProjection[1]);
  out.farProjection =
      replace_fov(left.farProjection, left.engineFarProjection[1]);
  out.derived = multiply(out.current, out.projection);
  if (!native_camera_detail::finite(out.current) ||
      !native_camera_detail::finite(out.projection) ||
      !native_camera_detail::finite(out.farProjection) ||
      !native_camera_detail::finite(out.derived))
    return refuse("non-finite prediction");
  out.currentError =
      native_camera_detail::maximum_error(out.current, right.current);
  out.projectionError =
      native_camera_detail::maximum_error(out.projection, right.projection);
  out.farProjectionError = native_camera_detail::maximum_error(
      out.farProjection, right.farProjection);
  out.derivedError =
      native_camera_detail::maximum_error(out.derived, right.derived);
  out.available = true;
  out.reason = "comparison available; numeric hypothesis only";
  return out;
}
} // namespace lmuqv
