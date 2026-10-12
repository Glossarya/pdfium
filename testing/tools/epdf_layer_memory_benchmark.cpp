// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "testing/tools/epdf_layer_tool_common.h"

#include <stdint.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>

#include <fstream>
#endif

#include "public/cpp/fpdf_scopers.h"
#include "public/epdf_annot.h"
#include "public/fpdf_annot.h"
#include "public/fpdfview.h"

namespace {

size_t CurrentRssBytes() {
#if defined(__APPLE__)
  mach_task_basic_info_data_t info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
    return 0;
  }
  return static_cast<size_t>(info.resident_size);
#elif defined(__linux__)
  std::ifstream statm("/proc/self/statm");
  size_t total_pages = 0;
  size_t resident_pages = 0;
  statm >> total_pages >> resident_pages;
  const long page_size = sysconf(_SC_PAGESIZE);
  if (!statm || page_size <= 0) {
    return 0;
  }
  return resident_pages * static_cast<size_t>(page_size);
#else
  return 0;
#endif
}

std::vector<size_t> ParseLayerCounts(const std::string& spec) {
  std::vector<size_t> result;
  size_t start = 0;
  while (start <= spec.size()) {
    const size_t comma = spec.find(',', start);
    const std::string token = spec.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!token.empty()) {
      result.push_back(
          static_cast<size_t>(std::strtoull(token.c_str(), nullptr, 10)));
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

bool AddTextAnnotation(FPDF_DOCUMENT layer) {
  ScopedFPDFPage page(FPDF_LoadPage(layer, 0));
  if (!page) {
    return false;
  }
  ScopedFPDFAnnotation annot(EPDFPage_CreateAnnot(page.get(), FPDF_ANNOT_TEXT));
  return !!annot;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    epdf_layer_tool::PrintUsage(argv[0], "[--layers=1,10,100,1000]");
    return 2;
  }

  std::string path = argv[1];
  std::vector<size_t> layer_counts = {1, 10, 100, 1000};
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    constexpr char kLayersPrefix[] = "--layers=";
    if (arg.rfind(kLayersPrefix, 0) == 0) {
      layer_counts = ParseLayerCounts(arg.substr(sizeof(kLayersPrefix) - 1));
    } else {
      epdf_layer_tool::PrintUsage(argv[0], "[--layers=1,10,100,1000]");
      return 2;
    }
  }
  if (layer_counts.empty() || layer_counts.front() == 0) {
    std::fprintf(stderr, "Layer counts must be positive.\n");
    return 2;
  }

  std::vector<uint8_t> base_bytes;
  if (!epdf_layer_tool::ReadFile(path, &base_bytes)) {
    std::fprintf(stderr, "Failed to read %s\n", path.c_str());
    return 1;
  }

  FPDF_InitLibrary();
  epdf_layer_tool::MemoryFile base_file(&base_bytes);
  EPDF_BASE_DOCUMENT base =
      EPDF_LoadBaseDocument(base_file.file_access(), nullptr);
  if (!base) {
    std::fprintf(stderr, "Failed to load base document.\n");
    FPDF_DestroyLibrary();
    return 1;
  }

  const size_t baseline_rss = CurrentRssBytes();
  std::vector<ScopedFPDFDocument> layers;
  layers.reserve(layer_counts.back());

  std::puts(
      "layers,rss_bytes,delta_from_base_rss,bytes_per_layer,"
      "promoted_objects");
  size_t next_report = 0;
  for (size_t i = 1; i <= layer_counts.back(); ++i) {
    EPDFLayerOpenStatus status = EPDFLayerOpenStatus_kOpenFailed;
    ScopedFPDFDocument layer(
        EPDFLayer_OpenLayer(base, /*pFileAccess=*/nullptr, nullptr, &status));
    if (!layer || status != EPDFLayerOpenStatus_kSuccess) {
      std::fprintf(stderr, "Failed to open layer %zu.\n", i);
      EPDF_ReleaseBaseDocument(base);
      FPDF_DestroyLibrary();
      return 1;
    }
    if (!AddTextAnnotation(layer.get())) {
      std::fprintf(stderr, "Failed to mutate layer %zu.\n", i);
      EPDF_ReleaseBaseDocument(base);
      FPDF_DestroyLibrary();
      return 1;
    }
    layers.push_back(std::move(layer));

    if (i == layer_counts[next_report]) {
      size_t promoted_objects = 0;
      for (const auto& live_layer : layers) {
        promoted_objects += EPDFLayer_GetPromotedObjectCount(live_layer.get());
      }
      const size_t rss = CurrentRssBytes();
      const size_t delta = rss > baseline_rss ? rss - baseline_rss : 0;
      std::printf("%zu,%zu,%zu,%zu,%zu\n", i, rss, delta, delta / i,
                  promoted_objects);
      ++next_report;
      if (next_report == layer_counts.size()) {
        break;
      }
    }
  }

  layers.clear();
  EPDF_ReleaseBaseDocument(base);
  FPDF_DestroyLibrary();
  return 0;
}
