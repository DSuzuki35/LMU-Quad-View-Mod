#pragma once
#include <d3d11.h>
#include <cstdint>
namespace lmuqv {
// Arm before native device creation. Rendering remains native until the pinned
// engine supplies matching current-frame focus/preparation scopes.
bool native_sps_prepare(bool enable,void(*log)(const char*));
// Fresh owned device boundary, before it is handed to application code. The
// native factory adapter and owned integration fixtures use this same boundary.
bool native_sps_attach_owned_device(ID3D11DeviceContext*,uintptr_t image,void(*log)(const char*));
bool native_sps_device_matches(ID3D11DeviceContext*);
void native_sps_stop();
}
