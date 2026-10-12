// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_FPDFAPI_EDIT_CPDF_TYPE3_REDACTOR_H_
#define CORE_FPDFAPI_EDIT_CPDF_TYPE3_REDACTOR_H_

#include <set>

#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fxcrt/retain_ptr.h"

class CPDF_Page;

// A Type3 font's glyphs are content of their own: a glyph procedure draws
// anything - text in other Type3 fonts, forms, soft masks, patterns - with
// resources of the font's own or the content's. Once redaction took text in
// `fonts` out of `page`, every Type3 font the page reaches holds only what
// the page shows: each user - the page's own text, the forms it draws, its
// annotations' appearances, soft masks, patterns - draws with a copy holding
// only what that user shows. A copy keeps the glyphs shown, their names and
// text, the codes and widths; it owns the resources its glyphs read, only
// what they use, and the fonts and content they draw are in turn copies as
// shown. Glyph, mask and pattern content keeps its bytes. Other pages keep
// the originals. Returns whether anything on the page changed.
bool RedactType3Glyphs(CPDF_Page* page,
                       const std::set<RetainPtr<CPDF_Font>>& fonts);

#endif  // CORE_FPDFAPI_EDIT_CPDF_TYPE3_REDACTOR_H_
