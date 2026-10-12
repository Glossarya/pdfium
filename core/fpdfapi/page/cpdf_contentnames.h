// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_FPDFAPI_PAGE_CPDF_CONTENTNAMES_H_
#define CORE_FPDFAPI_PAGE_CPDF_CONTENTNAMES_H_

#include <stdint.h>

#include <map>
#include <set>

#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/span.h"

class CPDF_Dictionary;
class CPDF_Document;

// Resource names by the type of resource they name: "Font", "XObject",
// "ExtGState", "ColorSpace", "Pattern", "Shading".
using ResourceNamesByType = std::map<ByteString, std::set<ByteString>>;

// The resource names `content` uses, each under the type its operator looks
// it up in, as CPDF_StreamContentParser does: /F1 Tf a font, /Im0 Do an
// XObject, /GS0 gs a graphics state, /CS0 cs a colour space, /P0 scn a
// pattern, /Sh0 sh a shading, and the colour space an inline image names.
// An inline image's data is stepped over as the parser steps over it, so it
// is not read as operators. `resources` and `page_resources` are what the
// content looks names up in (CPDF_Form::ChooseResourcesOfType()), needed to
// measure an inline image in a colour space it names.
ResourceNamesByType GetResourceNamesInContent(
    CPDF_Document* document,
    pdfium::span<const uint8_t> content,
    const CPDF_Dictionary* resources,
    const CPDF_Dictionary* page_resources);

#endif  // CORE_FPDFAPI_PAGE_CPDF_CONTENTNAMES_H_
