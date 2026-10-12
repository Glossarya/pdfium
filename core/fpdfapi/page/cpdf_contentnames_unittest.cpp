// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/page/cpdf_contentnames.h"

#include <memory>
#include <set>

#include "core/fpdfapi/page/test_with_page_module.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_test_document.h"
#include "testing/gtest/include/gtest/gtest.h"

using CPDFContentNamesTest = TestWithPageModule;

namespace {

ResourceNamesByType NamesIn(CPDF_Document* document,
                            ByteStringView content,
                            const CPDF_Dictionary* resources) {
  return GetResourceNamesInContent(document, content.unsigned_span(), resources,
                                   resources);
}

}  // namespace

TEST_F(CPDFContentNamesTest, NamesEachNameUnderItsOperatorsType) {
  auto document = std::make_unique<CPDF_TestDocument>();
  // A name inside a string is text, and a marked-content tag or property
  // list no resource pruning covers; #20 decodes.
  EXPECT_EQ((ResourceNamesByType{{"ExtGState", {"GS0"}},
                                 {"ColorSpace", {"CS0", "Spot 1"}},
                                 {"Pattern", {"P0"}},
                                 {"Font", {"F1"}},
                                 {"XObject", {"Im0"}},
                                 {"Shading", {"Sh0"}}}),
            NamesIn(document.get(),
                    "q /GS0 gs /CS0 cs 0 1 0 /P0 scn BT /F1 12 Tf "
                    "(a /NotAName) Tj ET /OC /MC0 BDC /Im0 Do EMC "
                    "/Spot#201 cs /Sh0 sh Q",
                    nullptr));
}

// One name for two resources of different types: each counts only as what
// its operator uses it as.
TEST_F(CPDFContentNamesTest, KeepsTypesOfOneNameApart) {
  auto document = std::make_unique<CPDF_TestDocument>();
  EXPECT_EQ((ResourceNamesByType{{"Font", {"R1"}}}),
            NamesIn(document.get(), "BT /R1 12 Tf (x) Tj ET", nullptr));
}

TEST_F(CPDFContentNamesTest, StepsOverInlineImageData) {
  auto document = std::make_unique<CPDF_TestDocument>();
  // One component of 8 bits: the image's four bytes are data, though they
  // read like a string and a name.
  auto resources = pdfium::MakeRetain<CPDF_Dictionary>();
  auto cal_gray = resources->SetNewFor<CPDF_Dictionary>("ColorSpace")
                      ->SetNewFor<CPDF_Array>("CSg");
  cal_gray->AppendNew<CPDF_Name>("CalGray");
  auto white_point =
      cal_gray->AppendNew<CPDF_Dictionary>()->SetNewFor<CPDF_Array>(
          "WhitePoint");
  white_point->AppendNew<CPDF_Number>(0.9505f);
  white_point->AppendNew<CPDF_Number>(1.0f);
  white_point->AppendNew<CPDF_Number>(1.089f);

  EXPECT_EQ(
      (ResourceNamesByType{{"ColorSpace", {"CSg"}}, {"XObject", {"Im0"}}}),
      NamesIn(document.get(),
              "q BI /W 4 /H 1 /CS /CSg /BPC 8 ID ((/X EI /Im0 Do Q",
              resources.Get()));
}
