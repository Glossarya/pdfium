// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/edit/cpdf_redaction_copies.h"

#include <array>

#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_document_view_scope.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"

namespace {

constexpr std::array<const char*, 4> kCopyKeys = {"Thumb", "PieceInfo",
                                                  "Metadata", "OPI"};

constexpr char kThumbnails[] = "Thumbnails";

// One piece of markup in XML text: a start tag, an end tag, or anything else
// that starts with '<' (a comment, CDATA, a processing instruction, a
// declaration).
struct Markup {
  enum class Kind { kStartTag, kEndTag, kOther };
  Kind kind = Kind::kOther;
  size_t begin = 0;     // The '<'.
  size_t end = 0;       // One past the closing '>'.
  ByteStringView name;  // A tag's qualified name.
  bool self_closing = false;
};

bool StartsAt(ByteStringView text, size_t at, ByteStringView prefix) {
  return text.Substr(at, prefix.GetLength()) == prefix;
}

std::optional<size_t> FindFrom(ByteStringView text,
                               ByteStringView needle,
                               size_t from) {
  for (size_t at = from; at + needle.GetLength() <= text.GetLength(); ++at) {
    if (StartsAt(text, at, needle)) {
      return at;
    }
  }
  return std::nullopt;
}

bool EndsName(char c) {
  return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\r' ||
         c == '\n';
}

// The local part of a qualified name: `Thumbnails` for `xmp:Thumbnails`.
ByteStringView LocalName(ByteStringView name) {
  for (size_t at = name.GetLength(); at > 0; --at) {
    if (name[at - 1] == ':') {
      return name.Substr(at);
    }
  }
  return name;
}

// The markup starting at the first '<' at or after `from`, or nullopt when
// there is none. Markup that never ends sets `broken`.
std::optional<Markup> NextMarkup(ByteStringView text,
                                 size_t from,
                                 bool* broken) {
  std::optional<size_t> begin = FindFrom(text, "<", from);
  if (!begin.has_value()) {
    return std::nullopt;
  }
  Markup markup;
  markup.begin = begin.value();
  auto ending_with = [&](ByteStringView opening,
                         ByteStringView closing) -> std::optional<Markup> {
    std::optional<size_t> at =
        FindFrom(text, closing, markup.begin + opening.GetLength());
    if (!at.has_value()) {
      *broken = true;
      return std::nullopt;
    }
    markup.end = at.value() + closing.GetLength();
    return markup;
  };
  if (StartsAt(text, markup.begin, "<!--")) {
    return ending_with("<!--", "-->");
  }
  if (StartsAt(text, markup.begin, "<![CDATA[")) {
    return ending_with("<![CDATA[", "]]>");
  }
  if (StartsAt(text, markup.begin, "<?")) {
    return ending_with("<?", "?>");
  }
  if (StartsAt(text, markup.begin, "<!")) {
    return ending_with("<!", ">");
  }

  const bool end_tag = StartsAt(text, markup.begin, "</");
  const size_t name_begin = markup.begin + (end_tag ? 2 : 1);
  size_t name_end = name_begin;
  while (name_end < text.GetLength() && !EndsName(text[name_end])) {
    ++name_end;
  }
  markup.kind = end_tag ? Markup::Kind::kEndTag : Markup::Kind::kStartTag;
  markup.name = text.Substr(name_begin, name_end - name_begin);
  // The tag ends at the first '>' outside a quoted attribute value.
  char quote = 0;
  for (size_t at = name_end; at < text.GetLength(); ++at) {
    const char c = text[at];
    if (quote) {
      quote = c == quote ? 0 : quote;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '>') {
      markup.end = at + 1;
      markup.self_closing = !end_tag && text[at - 1] == '/';
      return markup;
    }
  }
  *broken = true;
  return std::nullopt;
}

// One past the end tag that closes the element `start` opens.
std::optional<size_t> ElementEnd(ByteStringView text, const Markup& start) {
  if (start.self_closing) {
    return start.end;
  }
  size_t depth = 0;
  bool broken = false;
  for (std::optional<Markup> markup = NextMarkup(text, start.end, &broken);
       markup.has_value(); markup = NextMarkup(text, markup->end, &broken)) {
    if (markup->kind == Markup::Kind::kOther || markup->name != start.name) {
      continue;
    }
    if (markup->kind == Markup::Kind::kStartTag) {
      depth += markup->self_closing ? 0 : 1;
    } else if (depth == 0) {
      return markup->end;
    } else {
      --depth;
    }
  }
  return std::nullopt;
}

// Whether `packet` names Thumbnails, read with its NUL bytes skipped: that
// finds the name in UTF-16 and UTF-32 text too.
bool MentionsThumbnails(pdfium::span<const uint8_t> packet) {
  const ByteStringView name(kThumbnails);
  size_t matched = 0;
  for (uint8_t byte : packet) {
    if (byte == 0) {
      continue;
    }
    // No letter repeats the name's first one, so a mismatch restarts at it.
    if (byte == static_cast<uint8_t>(name[matched])) {
      ++matched;
    } else {
      matched = byte == static_cast<uint8_t>(name[0]) ? 1 : 0;
    }
    if (matched == name.GetLength()) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool HasContentCopies(const CPDF_Dictionary* dict) {
  if (!dict) {
    return false;
  }
  for (const char* key : kCopyKeys) {
    if (dict->KeyExist(key)) {
      return true;
    }
  }
  return false;
}

void RemoveContentCopies(CPDF_Dictionary* dict) {
  if (!dict) {
    return;
  }
  for (const char* key : kCopyKeys) {
    dict->RemoveFor(key);
  }
}

RetainPtr<CPDF_Stream> CopyWithoutContentCopies(const CPDF_Stream* stream) {
  RetainPtr<CPDF_Stream> copy = stream ? ToStream(stream->Clone()) : nullptr;
  if (copy) {
    RemoveContentCopies(copy->GetMutableDict().Get());
  }
  return copy;
}

void RemovePageContentCopies(CPDF_Page* page) {
  if (page && HasContentCopies(page->GetDict().Get())) {
    RemoveContentCopies(page->GetMutableDict().Get());
  }
}

std::optional<ByteString> RemoveXmpThumbnails(ByteStringView packet) {
  if (packet.Contains('\0')) {
    return std::nullopt;
  }
  ByteString kept;
  size_t copied = 0;
  bool broken = false;
  for (std::optional<Markup> markup = NextMarkup(packet, 0, &broken);
       markup.has_value(); markup = NextMarkup(packet, markup->end, &broken)) {
    if (markup->kind != Markup::Kind::kStartTag ||
        LocalName(markup->name) != ByteStringView(kThumbnails)) {
      continue;
    }
    std::optional<size_t> end = ElementEnd(packet, markup.value());
    if (!end.has_value()) {
      return std::nullopt;
    }
    kept += packet.Substr(copied, markup->begin - copied);
    copied = end.value();
    markup->end = end.value();
  }
  if (broken) {
    return std::nullopt;
  }
  kept += packet.Substr(copied);
  return kept;
}

void RemoveDocumentXmpThumbnails(CPDF_Document* doc) {
  if (!doc) {
    return;
  }
  // References a base document's objects hold resolve through the layer.
  CPDF_DocumentViewScope document_view(doc);
  const CPDF_Dictionary* root = doc->GetRoot();
  RetainPtr<const CPDF_Stream> metadata =
      root ? root->GetStreamFor("Metadata") : nullptr;
  if (!metadata) {
    return;
  }
  auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(metadata);
  acc->LoadAllDataFiltered();
  if (!MentionsThumbnails(acc->GetSpan())) {
    return;
  }

  std::optional<ByteString> kept =
      RemoveXmpThumbnails(ByteStringView(acc->GetSpan()));
  if (kept.has_value() && kept->GetLength() == acc->GetSize()) {
    return;
  }
  // The stream is written through its object number, so on a layer the
  // write lands in the layer's copy and never in the base's.
  RetainPtr<const CPDF_Reference> reference =
      ToReference(root->GetObjectFor("Metadata"));
  RetainPtr<CPDF_Stream> writable =
      kept.has_value() && reference
          ? ToStream(doc->GetMutableIndirectObject(reference->GetRefObjNum()))
          : nullptr;
  if (!writable) {
    doc->GetMutableRoot()->RemoveFor("Metadata");
    return;
  }
  writable->SetDataAndRemoveFilter(kept->unsigned_span());
}
