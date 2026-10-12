// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef FPDFSDK_EPDF_ANNOT_HELPERS_H_
#define FPDFSDK_EPDF_ANNOT_HELPERS_H_

#include "core/fxcrt/retain_ptr.h"
#include "public/fpdfview.h"

class CPDF_Dictionary;

namespace epdf {

// The dictionary behind |annot|, or null for a null or closed handle. The same
// as upstream's helpers of these names in fpdf_annot.cpp, for our files.
const CPDF_Dictionary* GetAnnotDictFromFPDFAnnotation(FPDF_ANNOTATION annot);
RetainPtr<CPDF_Dictionary> GetMutableAnnotDictFromFPDFAnnotation(
    FPDF_ANNOTATION annot);

}  // namespace epdf

#endif  // FPDFSDK_EPDF_ANNOT_HELPERS_H_
