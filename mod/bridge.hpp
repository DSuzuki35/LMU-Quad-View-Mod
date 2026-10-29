#pragma once
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_NO_PROTOTYPES
#include <windows.h>
#include <d3d11.h>
#include "openxr/openxr.h"

namespace lmuqv {
// XR side: frame cache valid only from successful locate to end-frame on rendering thread.
bool xr_quad_active();
// Runtime-only selected dimensions. Available before hooks start; exact per eye.
// False in legacy mode or before validated runtime recommendations are available.
bool xr_runtime_sizes(unsigned eye,uint32_t& outerWidth,uint32_t& outerHeight,uint32_t& focusWidth,uint32_t& focusHeight);
bool xr_frame_views(XrView (&views)[4]);
// Copies source (resolved single-sample color) to acquired outer image for eye 0/1.
// XR side owns acquisition/release; returns false if not active or incompatible.
bool xr_copy_outer(unsigned eye, ID3D11Texture2D* source);
// Borrowed RTV for the captured outer image, still acquired until xrEndFrame.
ID3D11RenderTargetView* xr_outer_rtv(unsigned eye);
void xr_scene_focus_complete(unsigned eye);
void log_line(const char* message);

// Engine side, called after successful xrCreateSession and before xrDestroySession.
// No hook in trace mode. Never patch on unrecognized executable/build.
bool engine_start();
void engine_stop();
}
