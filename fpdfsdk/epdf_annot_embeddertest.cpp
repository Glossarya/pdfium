// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

// The annotation API in public/epdf_annot.h.

#include "public/epdf_annot.h"

#include <limits>

#include "core/fpdfapi/page/cpdf_annotcontext.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fxcrt/fx_coordinates.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "public/cpp/fpdf_scopers.h"
#include "public/fpdf_annot.h"
#include "public/fpdf_edit.h"
#include "public/fpdfview.h"
#include "testing/embedder_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/utils/file_util.h"

namespace {

// The number |annot|'s dictionary holds under |key|, or null.
RetainPtr<const CPDF_Number> NumberFor(FPDF_ANNOTATION annot, const char* key) {
  const CPDF_Dictionary* dict =
      CPDFAnnotContextFromFPDFAnnotation(annot)->GetAnnotDict();
  return ToNumber(dict->GetObjectFor(key));
}

}  // namespace

class EPDFAnnotEmbedderTest : public EmbedderTest {};

TEST_F(EPDFAnnotEmbedderTest, SetNumberValueStoresTheNumberAsGiven) {
  ScopedFPDFDocument doc(FPDF_CreateNewDocument());
  ASSERT_TRUE(doc);
  ScopedFPDFPage page(FPDFPage_New(doc.get(), 0, 612, 792));
  ASSERT_TRUE(page);
  ScopedFPDFAnnotation annot(
      FPDFPage_CreateAnnot(page.get(), FPDF_ANNOT_SQUARE));
  ASSERT_TRUE(annot);

  // A whole number is written as a PDF integer: /MaxLen 5, not 5.0.
  ASSERT_TRUE(EPDFAnnot_SetNumberValue(annot.get(), "MaxLen", 5));
  RetainPtr<const CPDF_Number> max_len = NumberFor(annot.get(), "MaxLen");
  ASSERT_TRUE(max_len);
  EXPECT_TRUE(max_len->IsInteger());
  EXPECT_EQ(5, max_len->GetInteger());

  ASSERT_TRUE(EPDFAnnot_SetNumberValue(annot.get(), "Rotate", -90));
  RetainPtr<const CPDF_Number> rotate = NumberFor(annot.get(), "Rotate");
  ASSERT_TRUE(rotate);
  EXPECT_TRUE(rotate->IsInteger());
  EXPECT_EQ(-90, rotate->GetInteger());

  // Any other value is written as a real, and reads back as given.
  ASSERT_TRUE(EPDFAnnot_SetNumberValue(annot.get(), "CA", 0.25f));
  RetainPtr<const CPDF_Number> opacity = NumberFor(annot.get(), "CA");
  ASSERT_TRUE(opacity);
  EXPECT_FALSE(opacity->IsInteger());
  float value = 0;
  ASSERT_TRUE(FPDFAnnot_GetNumberValue(annot.get(), "CA", &value));
  EXPECT_EQ(0.25f, value);

  // So is a whole number too large for a PDF integer.
  ASSERT_TRUE(EPDFAnnot_SetNumberValue(annot.get(), "Big", 3e9f));
  RetainPtr<const CPDF_Number> big = NumberFor(annot.get(), "Big");
  ASSERT_TRUE(big);
  EXPECT_FALSE(big->IsInteger());
  EXPECT_EQ(3e9f, big->GetNumber());
}

TEST_F(EPDFAnnotEmbedderTest, SetNumberValueRefusesWithoutWriting) {
  ScopedFPDFDocument doc(FPDF_CreateNewDocument());
  ASSERT_TRUE(doc);
  ScopedFPDFPage page(FPDFPage_New(doc.get(), 0, 612, 792));
  ASSERT_TRUE(page);
  ScopedFPDFAnnotation annot(
      FPDFPage_CreateAnnot(page.get(), FPDF_ANNOT_SQUARE));
  ASSERT_TRUE(annot);

  EXPECT_FALSE(EPDFAnnot_SetNumberValue(nullptr, "CA", 1));
  EXPECT_FALSE(EPDFAnnot_SetNumberValue(annot.get(), nullptr, 1));
  EXPECT_FALSE(EPDFAnnot_SetNumberValue(annot.get(), "", 1));
  EXPECT_FALSE(EPDFAnnot_SetNumberValue(
      annot.get(), "CA", std::numeric_limits<float>::quiet_NaN()));
  EXPECT_FALSE(EPDFAnnot_SetNumberValue(
      annot.get(), "CA", std::numeric_limits<float>::infinity()));
  EXPECT_FALSE(FPDFAnnot_HasKey(annot.get(), "CA"));
  EXPECT_FALSE(CPDFAnnotContextFromFPDFAnnotation(annot.get())
                   ->GetAnnotDict()
                   ->KeyExist(""));
}

// On a layer, a refused call leaves the base's annotation where it is; an
// accepted one copies it up.
TEST_F(EPDFAnnotEmbedderTest, RefusedSetNumberValueCopiesNothingUp) {
  FileAccessForTesting base_access("redact_annot.pdf");
  EPDF_BASE_DOCUMENT base = EPDF_LoadBaseDocument(&base_access, nullptr);
  ASSERT_TRUE(base);
  {
    ScopedFPDFDocument layer(
        EPDFLayer_OpenLayer(base, nullptr, nullptr, nullptr));
    ASSERT_TRUE(layer);
    ScopedFPDFPage page(FPDF_LoadPage(layer.get(), 0));
    ASSERT_TRUE(page);
    ScopedFPDFAnnotation annot(FPDFPage_GetAnnot(page.get(), 0));
    ASSERT_TRUE(annot);
    const unsigned int obj_num = EPDFAnnot_GetObjectNumber(annot.get());
    ASSERT_NE(0u, obj_num);

    EXPECT_FALSE(EPDFAnnot_SetNumberValue(
        annot.get(), "CA", std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(EPDFAnnot_SetNumberValue(annot.get(), "", 1));
    EXPECT_FALSE(EPDFLayer_IsObjectPromoted(layer.get(), obj_num));

    EXPECT_TRUE(EPDFAnnot_SetNumberValue(annot.get(), "CA", 0.5f));
    EXPECT_TRUE(EPDFLayer_IsObjectPromoted(layer.get(), obj_num));
  }
  EPDF_ReleaseBaseDocument(base);
}

// A read on a layer sees the layer's version of what an annotation names. Here
// the annotation is still the base's, while its appearance stream has been
// changed in the layer: the matrix read back is the layer's.
TEST_F(EPDFAnnotEmbedderTest, ReadsResolveThroughTheLayer) {
  FileAccessForTesting base_access("annotation_highlight_square_with_ap.pdf");
  EPDF_BASE_DOCUMENT base = EPDF_LoadBaseDocument(&base_access, nullptr);
  ASSERT_TRUE(base);
  {
    ScopedFPDFDocument layer(
        EPDFLayer_OpenLayer(base, nullptr, nullptr, nullptr));
    ASSERT_TRUE(layer);
    ScopedFPDFPage page(FPDF_LoadPage(layer.get(), 0));
    ASSERT_TRUE(page);
    ScopedFPDFAnnotation annot(FPDFPage_GetAnnot(page.get(), 0));
    ASSERT_TRUE(annot);

    // The normal appearance's object number, read without resolving it.
    RetainPtr<const CPDF_Dictionary> ap =
        CPDFAnnotContextFromFPDFAnnotation(annot.get())
            ->GetAnnotDict()
            ->GetDictFor("AP");
    ASSERT_TRUE(ap);
    RetainPtr<const CPDF_Reference> normal = ToReference(ap->GetObjectFor("N"));
    ASSERT_TRUE(normal);
    const uint32_t stream_num = normal->GetRefObjNum();

    RetainPtr<CPDF_Stream> stream =
        ToStream(CPDFDocumentFromFPDFDocument(layer.get())
                     ->GetMutableIndirectObject(stream_num));
    ASSERT_TRUE(stream);
    stream->GetMutableDict()->SetMatrixFor("Matrix",
                                           CFX_Matrix(2, 0, 0, 2, 10, 20));
    ASSERT_TRUE(EPDFLayer_IsObjectPromoted(layer.get(), stream_num));
    ASSERT_FALSE(EPDFLayer_IsObjectPromoted(
        layer.get(), EPDFAnnot_GetObjectNumber(annot.get())));

    FS_MATRIX matrix;
    ASSERT_TRUE(EPDFAnnot_GetAPMatrix(
        annot.get(), FPDF_ANNOT_APPEARANCEMODE_NORMAL, &matrix));
    EXPECT_EQ(2, matrix.a);
    EXPECT_EQ(0, matrix.b);
    EXPECT_EQ(0, matrix.c);
    EXPECT_EQ(2, matrix.d);
    EXPECT_EQ(10, matrix.e);
    EXPECT_EQ(20, matrix.f);
  }
  EPDF_ReleaseBaseDocument(base);
}
