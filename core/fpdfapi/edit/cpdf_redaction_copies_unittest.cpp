// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/edit/cpdf_redaction_copies.h"

#include <optional>
#include <string>

#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// An XMP packet whose rdf:Description holds `properties`.
std::string Packet(const std::string& properties) {
  return "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
         "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
         "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
         "<rdf:Description rdf:about=\"\" "
         "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" "
         "xmlns:dc=\"http://purl.org/dc/elements/1.1/\">" +
         properties +
         "</rdf:Description></rdf:RDF></x:xmpmeta>\n"
         "<?xpacket end=\"w\"?>";
}

const char kThumbnail[] =
    "<xmp:Thumbnails><rdf:Alt><rdf:li rdf:parseType=\"Resource\">"
    "<xmpGImg:format>JPEG</xmpGImg:format>"
    "<xmpGImg:image>U0VDUkVUUElYRUxT</xmpGImg:image>"
    "</rdf:li></rdf:Alt></xmp:Thumbnails>";

const char kTitle[] =
    "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">Report</rdf:li>"
    "</rdf:Alt></dc:title>";

std::optional<std::string> Removed(const std::string& packet) {
  std::optional<ByteString> kept =
      RemoveXmpThumbnails(ByteStringView(packet.c_str()));
  if (!kept.has_value()) {
    return std::nullopt;
  }
  return std::string(kept->c_str(), kept->GetLength());
}

}  // namespace

TEST(CPDFRedactionCopiesTest, RemovesTheThumbnailsElementAndKeepsTheRest) {
  EXPECT_EQ(Packet(kTitle), Removed(Packet(std::string(kTitle) + kThumbnail)));
}

TEST(CPDFRedactionCopiesTest, LeavesAPacketWithoutThumbnailsAsItIs) {
  EXPECT_EQ(Packet(kTitle), Removed(Packet(kTitle)));
}

TEST(CPDFRedactionCopiesTest, RemovesThumbnailsUnderAnyPrefix) {
  EXPECT_EQ(Packet(kTitle),
            Removed(Packet(std::string("<xap:Thumbnails><rdf:Alt/>"
                                       "</xap:Thumbnails>") +
                           kTitle + "<Thumbnails>x</Thumbnails>")));
}

TEST(CPDFRedactionCopiesTest, RemovesEveryThumbnailsElement) {
  const std::string two_descriptions =
      Packet(kThumbnail) + Packet(std::string(kTitle) + kThumbnail);
  EXPECT_EQ(Packet("") + Packet(kTitle), Removed(two_descriptions));
}

TEST(CPDFRedactionCopiesTest, RemovesAnEmptyThumbnailsElement) {
  EXPECT_EQ(Packet(kTitle),
            Removed(Packet(std::string("<xmp:Thumbnails/>") + kTitle)));
}

TEST(CPDFRedactionCopiesTest, ReadsAQuotedGreaterThanAsPartOfTheTag) {
  EXPECT_EQ(Packet(kTitle),
            Removed(Packet(std::string("<xmp:Thumbnails a=\"1 > 0\">"
                                       "<rdf:Alt/></xmp:Thumbnails>") +
                           kTitle)));
}

TEST(CPDFRedactionCopiesTest, KeepsOtherNamesThatContainThumbnails) {
  const std::string packet = Packet(
      "<xmp:ThumbnailsCount>1</xmp:ThumbnailsCount>"
      "<!-- <xmp:Thumbnails> --><dc:description>Thumbnails"
      "</dc:description>");
  EXPECT_EQ(packet, Removed(packet));
}

TEST(CPDFRedactionCopiesTest, RefusesAThumbnailsElementWithoutAnEnd) {
  EXPECT_EQ(std::nullopt,
            Removed(Packet("<xmp:Thumbnails><rdf:Alt></rdf:Alt>")));
}

TEST(CPDFRedactionCopiesTest, RefusesMarkupThatNeverEnds) {
  EXPECT_EQ(std::nullopt, Removed(Packet(kTitle) + "<!-- open"));
}

TEST(CPDFRedactionCopiesTest, RefusesTextThatIsNotUtf8) {
  const std::string utf16 = std::string("\xFE\xFF\0<\0x", 6);
  EXPECT_EQ(std::nullopt,
            RemoveXmpThumbnails(ByteStringView(pdfium::as_byte_span(utf16))));
}

TEST(CPDFRedactionCopiesTest, FindsAndRemovesTheCopiesOfADictionary) {
  auto page = pdfium::MakeRetain<CPDF_Dictionary>();
  page->SetNewFor<CPDF_Name>("Type", "Page");
  EXPECT_FALSE(HasContentCopies(page.Get()));
  for (const char* key : {"Thumb", "PieceInfo", "Metadata", "OPI"}) {
    page->SetNewFor<CPDF_Name>(key, "x");
    EXPECT_TRUE(HasContentCopies(page.Get())) << key;
    RemoveContentCopies(page.Get());
    EXPECT_FALSE(page->KeyExist(key)) << key;
  }
  EXPECT_TRUE(page->KeyExist("Type"));
}
