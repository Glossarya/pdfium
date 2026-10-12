// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/edit/cpdf_tounicodecmap.h"

#include <utility>

#include "core/fpdfapi/font/cpdf_tounicodemap.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fxcrt/data_vector.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

CPDF_ToUnicodeMap Read(const ByteString& cmap) {
  pdfium::span<const uint8_t> bytes = cmap.unsigned_span();
  return CPDF_ToUnicodeMap(pdfium::MakeRetain<CPDF_Stream>(
      DataVector<uint8_t>(bytes.begin(), bytes.end()),
      pdfium::MakeRetain<CPDF_Dictionary>()));
}

}  // namespace

TEST(CPDFToUnicodeCMapTest, ReadsBackEachCodesText) {
  std::map<uint8_t, WideString> texts;
  texts[0x41] = L"A";
  texts[0x42] = L"fi";  // more than one character
  texts[0x43] = WideString::FromUTF8("\xF0\x9F\x98\x80");  // beyond the BMP
  texts[0x44] = L"";                                       // left out
  const CPDF_ToUnicodeMap map = Read(CreateSimpleFontToUnicodeCMap(texts));
  EXPECT_EQ(L"A", map.Lookup(0x41));
  EXPECT_EQ(L"fi", map.Lookup(0x42));
  // The reader keeps UTF-16 units: a surrogate pair, as a font's own map
  // gives it to the writer.
  WideString pair;
  pair += static_cast<wchar_t>(0xD83D);
  pair += static_cast<wchar_t>(0xDE00);
  EXPECT_EQ(pair, map.Lookup(0x43));
  EXPECT_EQ(L"", map.Lookup(0x44));
  EXPECT_EQ(L"", map.Lookup(0x45));
}

TEST(CPDFToUnicodeCMapTest, SplitsIntoBlocksOfAHundred) {
  std::map<uint8_t, WideString> texts;
  for (int code = 0; code < 256; ++code) {
    texts[static_cast<uint8_t>(code)] =
        WideString(static_cast<wchar_t>(0x4E00 + code));
  }
  const ByteString cmap = CreateSimpleFontToUnicodeCMap(texts);
  EXPECT_NE(cmap.Find("100 beginbfchar"), std::nullopt);
  EXPECT_NE(cmap.Find("56 beginbfchar"), std::nullopt);
  const CPDF_ToUnicodeMap map = Read(cmap);
  for (int code = 0; code < 256; ++code) {
    EXPECT_EQ(WideString(static_cast<wchar_t>(0x4E00 + code)),
              map.Lookup(code));
  }
}
