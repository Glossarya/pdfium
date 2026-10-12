// Copyright 2025 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_FPDFAPI_EDIT_CPDF_TEXT_REDACTOR_H_
#define CORE_FPDFAPI_EDIT_CPDF_TEXT_REDACTOR_H_

#include <array>
#include <optional>
#include <set>

#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/span.h"
#include "core/fxcrt/fx_coordinates.h"

class CPDF_Page;

// One redaction region in PAGE USER SPACE.
//
// `bbox` is always the region's axis-aligned bounds. When the marked area is
// NOT axis-aligned (rotated/sheared text marks), `has_quad` is set and `quad`
// carries the exact oriented corners in FS_QUADPOINTSF slot order:
// upper-start, upper-end, lower-start, lower-end.
//
// Text, vector paths, and shadings use the quad when present. Images remain
// conservatively bbox-based; black-box drawing uses the exact quad so the
// final overlay matches the marked area.
struct RedactRegion {
  CFX_FloatRect bbox;
  bool has_quad = false;
  std::array<CFX_PointF, 4> quad = {};
};

// Result of sanitizing page content for a redaction. `succeeded` is separate
// from `changed`: a valid redaction over an empty region succeeds without
// changing a page object, while a decoder/stream failure must abort the apply
// instead of being hidden behind an overlay.
struct RedactResult {
  bool succeeded = true;
  bool changed = false;
  // EmbedPDF: the Type3 fonts text lost glyphs of (RedactType3Glyphs()).
  std::set<RetainPtr<CPDF_Font>> type3_fonts;
};

// Build a region from FS_QUADPOINTSF-slot corners (US, UE, LS, LE).
// Well-formed oriented quads carry the exact quad; axis-aligned input takes
// the fast rect-only representation; malformed producer data degrades to the
// all-corner AABB (never a crash, never a leak).
std::optional<RedactRegion> RedactRegionFromQuadCorners(
    const std::array<CFX_PointF, 4>& corners);

// Does this region intersect an axis-aligned rect (open-interval overlap,
// exact against the oriented quad when present)? Used for annotation
// collateral so an annotation touching only the empty corner of a rotated
// mark's bounding box is NOT destroyed.
bool RedactRegionIntersectsRect(const RedactRegion& region,
                                const CFX_FloatRect& rect);

// Redacts (removes) glyphs from text objects that intersect the region(s).
// Inputs are in PAGE USER SPACE (same space as highlights).
// If `recurse_forms` is true, contents of Form XObjects used on the page
// are also scanned and redacted. Edits inside a form regenerate that form’s
// content stream immediately. The page stream is NOT regenerated here.
//
// Returns true if anything changed.
RedactResult RedactTextInRegions(CPDF_Page* page,
                                 pdfium::span<const RedactRegion> regions,
                                 bool recurse_forms,
                                 bool draw_black_boxes);

// Rect-only conveniences (regions with no oriented quads).
RedactResult RedactTextInRect(CPDF_Page* page,
                              const CFX_FloatRect& page_space_rect,
                              bool recurse_forms,
                              bool draw_black_boxes);

RedactResult RedactTextInRects(
    CPDF_Page* page,
    pdfium::span<const CFX_FloatRect> page_space_rects,
    bool recurse_forms,
    bool draw_black_boxes);

#endif  // CORE_FPDFAPI_EDIT_CPDF_TEXT_REDACTOR_H_
