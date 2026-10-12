// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "testing/tools/epdf_layer_tool_common.h"

#include <stdint.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "public/cpp/fpdf_scopers.h"
#include "public/epdf_annot.h"
#include "public/epdf_layer.h"
#include "public/fpdf_annot.h"
#include "public/fpdf_save.h"
#include "public/fpdfview.h"

namespace {

size_t ParseSizeArg(const std::string& arg,
                    const char* prefix,
                    size_t fallback) {
  const std::string prefix_string(prefix);
  if (arg.rfind(prefix_string, 0) != 0) {
    return fallback;
  }
  return static_cast<size_t>(
      std::strtoull(arg.substr(prefix_string.size()).c_str(), nullptr, 10));
}

bool AddTextAnnotations(FPDF_DOCUMENT layer, size_t count) {
  ScopedFPDFPage page(FPDF_LoadPage(layer, 0));
  if (!page) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    ScopedFPDFAnnotation annot(
        EPDFPage_CreateAnnot(page.get(), FPDF_ANNOT_TEXT));
    if (!annot) {
      return false;
    }
  }
  return static_cast<size_t>(FPDFPage_GetAnnotCount(page.get())) == count;
}

bool VerifyMaterializedAnnotCount(const std::vector<uint8_t>& materialized,
                                  size_t expected_count) {
  ScopedFPDFDocument reopened(FPDF_LoadMemDocument64(
      materialized.data(), materialized.size(), nullptr));
  if (!reopened) {
    return false;
  }
  ScopedFPDFPage page(FPDF_LoadPage(reopened.get(), 0));
  if (!page) {
    return false;
  }
  return static_cast<size_t>(FPDFPage_GetAnnotCount(page.get())) ==
         expected_count;
}

bool VerifyLayerAnnotCount(FPDF_DOCUMENT layer, size_t expected_count) {
  ScopedFPDFPage page(FPDF_LoadPage(layer, 0));
  if (!page) {
    return false;
  }
  return static_cast<size_t>(FPDFPage_GetAnnotCount(page.get())) ==
         expected_count;
}

bool VerifyRawDeltaReplay(EPDF_BASE_DOCUMENT base,
                          const std::string& delta,
                          size_t expected_count) {
  const std::vector<uint8_t> delta_bytes(delta.begin(), delta.end());
  epdf_layer_tool::MemoryFile delta_file(&delta_bytes);
  EPDFLayerOpenStatus open_status = EPDFLayerOpenStatus_kOpenFailed;
  ScopedFPDFDocument reopened(EPDFLayer_OpenLayer(
      base, delta_file.file_access(), nullptr, &open_status));
  return reopened && open_status == EPDFLayerOpenStatus_kSuccess &&
         VerifyLayerAnnotCount(reopened.get(), expected_count);
}

bool VerifyArtifactReplay(EPDF_BASE_DOCUMENT base,
                          FPDF_DOCUMENT layer,
                          size_t expected_count) {
  unsigned long artifact_size = 0;
  EPDFLayerSaveStatus save_status = EPDFLayerSaveStatus_kSaveFailed;
  void* artifact = EPDFLayer_SaveLayerArtifactToOwnedBuffer(
      layer, &artifact_size, &save_status);
  if (!artifact || artifact_size == 0 ||
      save_status != EPDFLayerSaveStatus_kSuccess) {
    EPDF_FreeBuffer(artifact);
    return false;
  }

  std::vector<uint8_t> artifact_bytes(
      static_cast<uint8_t*>(artifact),
      static_cast<uint8_t*>(artifact) + artifact_size);
  EPDF_FreeBuffer(artifact);

  epdf_layer_tool::MemoryFile artifact_file(&artifact_bytes);
  EPDFLayerOpenStatus open_status = EPDFLayerOpenStatus_kOpenFailed;
  ScopedFPDFDocument reopened(EPDFLayer_OpenLayerArtifact(
      base, artifact_file.file_access(), nullptr, &open_status));
  return reopened && open_status == EPDFLayerOpenStatus_kSuccess &&
         VerifyLayerAnnotCount(reopened.get(), expected_count);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    epdf_layer_tool::PrintUsage(
        argv[0],
        "[--layers=100] [--rounds=60] [--sleep-seconds=60] [--seed=N]");
    return 2;
  }

  std::string path = argv[1];
  size_t layer_count = 100;
  size_t rounds = 60;
  size_t sleep_seconds = 60;
  uint32_t seed = 0xE7DF750u;

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("--layers=", 0) == 0) {
      layer_count = ParseSizeArg(arg, "--layers=", layer_count);
    } else if (arg.rfind("--rounds=", 0) == 0) {
      rounds = ParseSizeArg(arg, "--rounds=", rounds);
    } else if (arg.rfind("--sleep-seconds=", 0) == 0) {
      sleep_seconds = ParseSizeArg(arg, "--sleep-seconds=", sleep_seconds);
    } else if (arg.rfind("--seed=", 0) == 0) {
      seed = static_cast<uint32_t>(ParseSizeArg(arg, "--seed=", seed));
    } else {
      epdf_layer_tool::PrintUsage(
          argv[0],
          "[--layers=100] [--rounds=60] [--sleep-seconds=60] [--seed=N]");
      return 2;
    }
  }

  if (layer_count == 0 || rounds == 0) {
    std::fprintf(stderr, "Layer count and rounds must be positive.\n");
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

  std::mt19937 rng(seed);
  std::uniform_int_distribution<size_t> layer_dist(0, layer_count - 1);
  std::uniform_int_distribution<size_t> edit_dist(1, 3);
  std::vector<size_t> expected_annots(layer_count, 0);

  for (size_t round = 0; round < rounds; ++round) {
    const size_t edited_layer = layer_dist(rng);
    expected_annots[edited_layer] += edit_dist(rng);

    for (size_t layer_index = 0; layer_index < layer_count; ++layer_index) {
      EPDFLayerOpenStatus open_status = EPDFLayerOpenStatus_kOpenFailed;
      ScopedFPDFDocument layer(
          EPDFLayer_OpenLayer(base, nullptr, nullptr, &open_status));
      if (!layer || open_status != EPDFLayerOpenStatus_kSuccess) {
        std::fprintf(stderr, "Round %zu layer %zu: open failed.\n", round,
                     layer_index);
        EPDF_ReleaseBaseDocument(base);
        FPDF_DestroyLibrary();
        return 1;
      }
      if (!AddTextAnnotations(layer.get(), expected_annots[layer_index])) {
        std::fprintf(stderr, "Round %zu layer %zu: mutation failed.\n", round,
                     layer_index);
        EPDF_ReleaseBaseDocument(base);
        FPDF_DestroyLibrary();
        return 1;
      }

      epdf_layer_tool::StringWriter writer;
      EPDFLayerSaveStatus save_status = EPDFLayerSaveStatus_kSaveFailed;
      if (!EPDFLayer_SaveDelta(layer.get(), &writer, &save_status) ||
          save_status != EPDFLayerSaveStatus_kSuccess) {
        std::fprintf(stderr, "Round %zu layer %zu: save failed (%d).\n", round,
                     layer_index, save_status);
        EPDF_ReleaseBaseDocument(base);
        FPDF_DestroyLibrary();
        return 1;
      }

      const std::vector<uint8_t> materialized =
          epdf_layer_tool::MaterializeLayerBytes(base_bytes, writer.data);
      if (!VerifyMaterializedAnnotCount(materialized,
                                        expected_annots[layer_index])) {
        std::fprintf(stderr,
                     "Round %zu layer %zu: materialized verification failed.\n",
                     round, layer_index);
        EPDF_ReleaseBaseDocument(base);
        FPDF_DestroyLibrary();
        return 1;
      }
      if (!VerifyRawDeltaReplay(base, writer.data,
                                expected_annots[layer_index])) {
        std::fprintf(stderr, "Round %zu layer %zu: raw delta replay failed.\n",
                     round, layer_index);
        EPDF_ReleaseBaseDocument(base);
        FPDF_DestroyLibrary();
        return 1;
      }
      if (!VerifyArtifactReplay(base, layer.get(),
                                expected_annots[layer_index])) {
        std::fprintf(stderr, "Round %zu layer %zu: artifact replay failed.\n",
                     round, layer_index);
        EPDF_ReleaseBaseDocument(base);
        FPDF_DestroyLibrary();
        return 1;
      }
    }

    std::printf("round=%zu layers=%zu edited_layer=%zu ok\n", round + 1,
                layer_count, edited_layer);
    if (round + 1 < rounds && sleep_seconds > 0) {
      std::this_thread::sleep_for(std::chrono::seconds(sleep_seconds));
    }
  }

  EPDF_ReleaseBaseDocument(base);
  FPDF_DestroyLibrary();
  return 0;
}
