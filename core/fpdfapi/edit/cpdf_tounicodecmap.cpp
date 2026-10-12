// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/edit/cpdf_tounicodecmap.h"

#include <algorithm>
#include <sstream>
#include <utility>
#include <vector>

#include "core/fxcrt/fx_extension.h"
#include "core/fxcrt/fx_string_wrappers.h"
#include "core/fxcrt/span.h"
#include "core/fxcrt/utf16.h"

namespace {

// A bfchar block holds at most 100 entries (PDF 32000-1, 9.7.5.4).
constexpr size_t kMaxEntriesPerBlock = 100;

constexpr char kStart[] =
    "/CIDInit /ProcSet findresource begin\n"
    "12 dict begin\n"
    "begincmap\n"
    "/CIDSystemInfo <</Registry (Adobe) /Ordering (UCS) /Supplement 0>> def\n"
    "/CMapName /Adobe-Identity-UCS def\n"
    "/CMapType 2 def\n"
    "1 begincodespacerange\n"
    "<00> <FF>\n"
    "endcodespacerange\n";

constexpr char kEnd[] =
    "endcmap\n"
    "CMapName currentdict /CMap defineresource pop\n"
    "end\n"
    "end\n";

// `text` in UTF-16BE, as hex digits. A 16-bit wchar_t already holds
// surrogate pairs; a wider one holds code points.
void WriteUTF16BE(fxcrt::ostringstream& buffer, const WideString& text) {
  for (wchar_t c : text) {
    const uint32_t unit = static_cast<uint32_t>(c);
    char hex[8];
    pdfium::span<const char> digits;
    if (unit <= 0xFFFF) {
      FXSYS_IntToFourHexChars(static_cast<uint16_t>(unit),
                              pdfium::span(hex).first<4u>());
      digits = pdfium::span(hex).first<4u>();
    } else if (unit <= pdfium::kMaximumSupplementaryCodePoint) {
      digits = FXSYS_ToUTF16BE(unit, hex);
    }
    for (char digit : digits) {
      buffer << digit;
    }
  }
}

}  // namespace

ByteString CreateSimpleFontToUnicodeCMap(
    const std::map<uint8_t, WideString>& texts) {
  std::vector<std::pair<uint8_t, WideString>> entries;
  for (const auto& [code, text] : texts) {
    if (!text.IsEmpty()) {
      entries.emplace_back(code, text);
    }
  }

  fxcrt::ostringstream buffer;
  buffer << kStart;
  for (size_t first = 0; first < entries.size(); first += kMaxEntriesPerBlock) {
    const size_t count = std::min(kMaxEntriesPerBlock, entries.size() - first);
    buffer << count << " beginbfchar\n";
    for (size_t i = first; i < first + count; ++i) {
      char code[2];
      FXSYS_IntToTwoHexChars(entries[i].first, code);
      buffer << "<" << code[0] << code[1] << "> <";
      WriteUTF16BE(buffer, entries[i].second);
      buffer << ">\n";
    }
    buffer << "endbfchar\n";
  }
  buffer << kEnd;
  return ByteString(buffer);
}
