// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_FPDFAPI_EDIT_CPDF_IMAGE_REDACTOR_H_
#define CORE_FPDFAPI_EDIT_CPDF_IMAGE_REDACTOR_H_

#include "core/fpdfapi/edit/cpdf_text_redactor.h"
#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/span.h"

class CPDF_Dictionary;
class CPDF_ImageObject;
class CPDF_Page;

// Redacts one image placement: every sample `page_rects` (page space) cover
// any part of becomes a fill, in the image and in its masks. The placement
// gets a private replacement, so other placements of the same image keep it
// whole. `resources` are those of the content drawing the image (a form's,
// else the page's), against which its /ColorSpace name resolves;
// `parent_to_page` maps that content's space to the page.
//
// An image keeps its colour space, bits per component and /Decode, and its
// hole is the colour space's paper (white, no ink). One whose filters leave
// plain samples (Flate, LZW, RunLength, the ASCII filters, and 1-bit CCITT or
// JBIG2) is written with Flate here, so a save copies its bytes; a JPEG stays
// a JPEG, its blocks away from the hole untouched (FillJpegRects()). Others
// (JPEG 2000, a JPEG libjpeg reads with a warning) are rewritten as RGB.
//
// A failure once the image is known to be hit (a decode that fails) is
// reported as a failure, so the caller never paints an overlay over pixels
// it could not remove.
RedactResult RedactImageObject(CPDF_Page* page,
                               CPDF_ImageObject* image_object,
                               const CPDF_Dictionary* resources,
                               pdfium::span<const CFX_FloatRect> page_rects,
                               const CFX_Matrix& parent_to_page,
                               bool fill_black);

#endif  // CORE_FPDFAPI_EDIT_CPDF_IMAGE_REDACTOR_H_
