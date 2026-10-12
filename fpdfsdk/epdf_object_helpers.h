// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef FPDFSDK_EPDF_OBJECT_HELPERS_H_
#define FPDFSDK_EPDF_OBJECT_HELPERS_H_

#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/retain_ptr.h"

class CPDF_AnnotContext;
class CPDF_Array;
class CPDF_Dictionary;
class CPDF_Document;
class CPDF_Page;

namespace epdf {

// The array |dict| holds under |key|, ready to write: an indirect one is
// copied up for writing (on a layer, into the open transaction), a direct one
// is written in place. When |dict| has none, a new direct array when
// |create_if_missing|, else null. |dict| must already be writable.
RetainPtr<CPDF_Array> GetMutableArrayMember(CPDF_Document* doc,
                                            CPDF_Dictionary* dict,
                                            ByteStringView key,
                                            bool create_if_missing);

// Whether |annotation| is a handle of an annotation of |page|: one opened in
// the same document, on the same page (the same page object, or the same
// dictionary when either has no object number). Object numbers alone don't
// say it: another document numbers its own objects.
bool IsAnnotationOfPage(CPDF_Page* page, CPDF_AnnotContext* annotation);

}  // namespace epdf

#endif  // FPDFSDK_EPDF_OBJECT_HELPERS_H_
