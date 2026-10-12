// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef PUBLIC_EPDF_REDACT_H_
#define PUBLIC_EPDF_REDACT_H_

#include <stdint.h>

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Experimental EmbedPDF Extension API.
// Apply redact annotations of a page, permanently removing the content under
// them: the `count` REDACT annotations at `marks`, or every REDACT annotation
// on the page when `marks` is NULL and `count` 0. Their regions are applied
// together, so each image under them is read and written once.
//
// Overlay precedence follows ISO 32000-2: if an annotation has an /RO
// (Redact Overlay) stream it is flattened as page content; otherwise an
// overlay is synthesized from the declarative entries (/IC fill and
// /OverlayText per /DA, /Q and /Repeat) and flattened the same way. An
// annotation with neither /RO nor declarative entries leaves the region
// transparent. Overlays are drawn in the order of the page's /Annots.
//
// Annotations whose /Rect intersects a redacted region with positive area
// are removed as well, including their popup cascade; removed widgets are
// detached from the AcroForm tree. REDACT annotations not applied are
// preserved. The applied annotations themselves are removed from the page.
//
// The caller is responsible for:
//   1. Closing the annotation handles with FPDFPage_CloseAnnot after this call
//   2. Calling FPDFPage_GenerateContent to persist changes
//
//   page                    - handle to a page
//   marks                   - REDACT annotations of `page`, or NULL for all
//   count                   - the number of annotations at `marks`; 0 when
//                             `marks` is NULL
//   out_removed_annot_count - optional, may be NULL. Receives the number of
//                             annotations removed as a side effect, NOT
//                             counting REDACT annotations. Zeroed on entry.
//
// Returns TRUE if redactions were applied. FALSE if there was none to apply,
// a mark isn't a REDACT annotation of `page`, or on error.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_ApplyRedactions(FPDF_PAGE page,
                         const FPDF_ANNOTATION* marks,
                         int count,
                         uint32_t* out_removed_annot_count);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_REDACT_H_
