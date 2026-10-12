// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "fpdfsdk/epdf_annot_helpers.h"

#include "core/fpdfapi/page/cpdf_annotcontext.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "fpdfsdk/cpdfsdk_helpers.h"

namespace epdf {

const CPDF_Dictionary* GetAnnotDictFromFPDFAnnotation(FPDF_ANNOTATION annot) {
  CPDF_AnnotContext* context = CPDFAnnotContextFromFPDFAnnotation(annot);
  return context ? context->GetAnnotDict() : nullptr;
}

RetainPtr<CPDF_Dictionary> GetMutableAnnotDictFromFPDFAnnotation(
    FPDF_ANNOTATION annot) {
  CPDF_AnnotContext* context = CPDFAnnotContextFromFPDFAnnotation(annot);
  return context ? context->GetMutableAnnotDict() : nullptr;
}

}  // namespace epdf
