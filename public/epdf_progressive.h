// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef PUBLIC_EPDF_PROGRESSIVE_H_
#define PUBLIC_EPDF_PROGRESSIVE_H_

// NOLINTNEXTLINE(build/include)
#include "fpdf_progressive.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// EmbedPDF's additions to public/fpdf_progressive.h: sliced renders with a
// matrix.

// Experimental EmbedPDF Extension API.
// Starts rendering `page` into `bitmap` as FPDF_RenderPageBitmapWithMatrix()
// does, and pauses once `budget_ms` milliseconds have passed, so a caller can
// look at other work, or cancel, between slices. However a render is sliced,
// the bitmap ends with the bytes FPDF_RenderPageBitmapWithMatrix() writes.
//
//   bitmap    - Handle to the bitmap to render into.
//   page      - Handle to the page.
//   matrix    - The transform, as for FPDF_RenderPageBitmapWithMatrix().
//   clipping  - The clip rectangle, as for FPDF_RenderPageBitmapWithMatrix().
//   flags     - 0 or a combination of the flags defined in fpdfview.h.
//   budget_ms - How long to render before pausing; 0 pauses at every chance.
//
// Returns FPDF_RENDER_TOBECONTINUED when paused, FPDF_RENDER_DONE, or
// FPDF_RENDER_FAILED, which it also returns when a render of `page` is in
// progress. Continue a paused render with EPDF_RenderPage_Continue(). Call
// FPDF_RenderPage_Close() after every start, whatever it returned; closing a
// paused render cancels it. Until then the page holds the render: nothing may
// change the page's document, render the page or reset its render cache, and
// `bitmap` must stay alive.
FPDF_EXPORT int FPDF_CALLCONV
EPDF_RenderPageBitmapWithMatrix_Start(FPDF_BITMAP bitmap,
                                      FPDF_PAGE page,
                                      const FS_MATRIX* matrix,
                                      const FS_RECTF* clipping,
                                      int flags,
                                      int budget_ms);

// Experimental EmbedPDF Extension API.
// Continues a render that EPDF_RenderPageBitmapWithMatrix_Start() started,
// pausing again once `budget_ms` milliseconds have passed.
//
//   page      - Handle to the page.
//   budget_ms - How long to render before pausing; 0 pauses at every chance.
//
// Returns FPDF_RENDER_TOBECONTINUED, FPDF_RENDER_DONE or FPDF_RENDER_FAILED.
FPDF_EXPORT int FPDF_CALLCONV EPDF_RenderPage_Continue(FPDF_PAGE page,
                                                       int budget_ms);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_PROGRESSIVE_H_
