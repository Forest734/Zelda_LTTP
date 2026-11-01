// [lttp] Smooth pixels (LinearFiltering = Smooth): the frame scaled up by a whole number with
// xBR, which rounds off the stair steps of the pixel art.
#pragma once
#include "types.h"

enum { kSmoothMaxScale = 4 };  // past it, a larger window gets the 4x scaled linear

// src is w x h, its rows src_pitch pixels apart (XRGB). dst is (w * scale) x (h * scale), its
// rows dst_pitch pixels apart. scale is 2 to kSmoothMaxScale.
void Smooth_Scale(const uint32 *src, int src_pitch, int w, int h,
                  uint32 *dst, int dst_pitch, int scale);
