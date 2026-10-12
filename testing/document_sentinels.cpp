// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "testing/document_sentinels.h"

#include <functional>
#include <set>
#include <vector>

#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// Calls `found` with the text of each stream (decoded), string and name
// (dictionary keys too) reachable from `start`, not following the keys
// `skipped`.
void ForEachReachableText(
    RetainPtr<const CPDF_Object> start,
    const std::set<ByteString>& skipped,
    const std::function<void(bool is_stream, const std::string&)>& found) {
  std::vector<RetainPtr<const CPDF_Object>> pending{std::move(start)};
  std::set<const CPDF_Object*> visited;
  while (!pending.empty()) {
    auto object = pending.back() ? pending.back()->GetDirect() : nullptr;
    pending.pop_back();
    if (!object || !visited.insert(object.Get()).second) {
      continue;
    }
    if (const CPDF_Stream* stream = object->AsStream()) {
      auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(pdfium::WrapRetain(stream));
      acc->LoadAllDataFiltered();
      const auto bytes = acc->GetSpan();
      found(/*is_stream=*/true, std::string(bytes.begin(), bytes.end()));
      pending.push_back(stream->GetDict());
    } else if (const CPDF_Dictionary* dict = object->AsDictionary()) {
      for (const auto& key : dict->GetKeys()) {
        found(/*is_stream=*/false, std::string(key.c_str(), key.GetLength()));
        if (!skipped.count(key)) {
          pending.push_back(dict->GetObjectFor(key.AsStringView()));
        }
      }
    } else if (const CPDF_Array* array = object->AsArray()) {
      for (size_t i = 0; i < array->size(); ++i) {
        pending.push_back(array->GetObjectAt(i));
      }
    } else if (object->IsString() || object->IsName()) {
      const ByteString value = object->GetString();
      found(/*is_stream=*/false, std::string(value.c_str(), value.GetLength()));
    }
  }
}

void ExpectNoSentinelFrom(RetainPtr<const CPDF_Object> start,
                          const std::set<ByteString>& skipped,
                          const std::string& sentinel) {
  ForEachReachableText(
      std::move(start), skipped, [&](bool, const std::string& text) {
        EXPECT_EQ(std::string::npos, text.find(sentinel)) << text;
      });
}

int CountSentinelFrom(RetainPtr<const CPDF_Object> start,
                      const std::set<ByteString>& skipped,
                      const std::string& sentinel) {
  int count = 0;
  ForEachReachableText(
      std::move(start), skipped, [&](bool is_stream, const std::string& text) {
        if (is_stream && text.find(sentinel) != std::string::npos) {
          ++count;
        }
      });
  return count;
}

RetainPtr<const CPDF_Object> PageDictionary(CPDF_Document* doc, int index) {
  RetainPtr<const CPDF_Dictionary> page = doc->GetPageDictionary(index);
  EXPECT_TRUE(page) << "no page " << index;
  return page;
}

}  // namespace

void ExpectNoSentinel(CPDF_Document* doc, const std::string& sentinel) {
  ExpectNoSentinelFrom(pdfium::WrapRetain(doc->GetRoot()), {}, sentinel);
}

int CountSentinel(CPDF_Document* doc, const std::string& sentinel) {
  return CountSentinelFrom(pdfium::WrapRetain(doc->GetRoot()), {}, sentinel);
}

void ExpectNoSentinelOnPage(CPDF_Document* doc,
                            int index,
                            const std::string& sentinel) {
  ExpectNoSentinelFrom(PageDictionary(doc, index), {"Parent"}, sentinel);
}

int CountSentinelOnPage(CPDF_Document* doc,
                        int index,
                        const std::string& sentinel) {
  return CountSentinelFrom(PageDictionary(doc, index), {"Parent"}, sentinel);
}
