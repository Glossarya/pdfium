// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_FPDFAPI_EDIT_CPDF_TOUNICODECMAP_H_
#define CORE_FPDFAPI_EDIT_CPDF_TOUNICODECMAP_H_

#include <stdint.h>

#include <map>

#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/widestring.h"

// A ToUnicode CMap for a simple font, whose character codes are one byte:
// each code in `texts` maps to its text, which can be more than one
// character (a ligature, or any string the file's writer chose). Codes with
// empty text are left out. CPDF_ToUnicodeMap reads it back.
ByteString CreateSimpleFontToUnicodeCMap(
    const std::map<uint8_t, WideString>& texts);

#endif  // CORE_FPDFAPI_EDIT_CPDF_TOUNICODECMAP_H_
