// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

// EmbedPDF: thread-confined runtime soak harness.
//
// Spawns N worker threads. Each runs an independent PDFium lifecycle in a
// loop, the way a server worker does (docs/embedpdf/fork-ledger.md, rule
// thread-confined-globals), and only ever touches handles it created:
//
//   EPDF_InitThread, decoded-image budget
//   -> base document + layer over it
//   -> read up to --pages pages: sliced render, text and char geometry,
//      annotations
//   -> read the form model
//   -> in a layer transaction: create a square and a rich-text FreeText
//      annotation, write a text field's value and redraw its widgets,
//      redact a region (--no-redact skips it)
//   -> render again, save the layer's delta, reopen a layer from it, render
//   -> encrypted save
//   -> close everything, EPDF_ShutdownThread
//
// Workers take the given PDFs in turn, so different threads work on different
// files at the same moment. --plain keeps the original loop (plain document,
// first page, encrypted save) for an A/B run with thread-local globals off.
//
// With embedpdf_thread_local_globals ON every thread owns its own PDFium state
// and the soak must pass under ThreadSanitizer: that is the gate before the
// server's worker pool grows (embedpdf repo, docs/conventions/
// runtime-confinement.md).

#include "testing/tools/epdf_layer_tool_common.h"

#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "public/cpp/fpdf_scopers.h"
#include "public/epdf_annot.h"
#include "public/epdf_form.h"
#include "public/epdf_layer.h"
#include "public/epdf_text.h"
#include "public/epdf_view.h"
#include "public/fpdf_annot.h"
#include "public/fpdf_progressive.h"
#include "public/fpdf_save.h"
#include "public/fpdf_text.h"
#include "public/fpdfview.h"

namespace {

// Fixed render surface keeps per-thread memory bounded while still exercising
// the rasterizer, font cache, and stock colorspaces.
constexpr int kRenderWidth = 300;
constexpr int kRenderHeight = 400;
constexpr int kRenderBudgetMs = 5;
constexpr unsigned long kDecodedImageBudget = 32ul << 20;
constexpr int kGeometryChars = 64;

constexpr char kRichTextJson[] =
    "{\"body\":{\"family\":\"Helvetica\",\"size\":12,\"color\":\"#102030\"},"
    "\"paragraphs\":[{\"runs\":[{\"text\":\"Soak \"},"
    "{\"text\":\"bold\",\"style\":{\"weight\":700}},"
    "{\"text\":\" H\"},{\"text\":\"2\",\"style\":{\"script\":\"sub\"}},"
    "{\"text\":\"O\"}]}]}";

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

struct Options {
  size_t threads = 4;
  size_t iterations = 50;
  size_t pages = 3;
  bool plain = false;
  bool render = true;
  bool encrypt = true;
  bool edit = true;
  bool redact = true;
};

const char* kUsageExtra =
    "[more.pdf ...] [--threads=4] [--iterations=50] [--pages=3] [--plain] "
    "[--no-render] [--no-encrypt] [--no-edit] [--no-redact]";

struct Worker {
  size_t index;
  size_t iteration;
  const std::string* file;

  bool Fail(const char* step) const {
    std::fprintf(stderr, "worker %zu iter %zu (%s): %s failed\n", index,
                 iteration, file->c_str(), step);
    return false;
  }
};

// The engine's render: sliced, with a budget per step.
bool RenderPageSliced(FPDF_PAGE page) {
  ScopedFPDFBitmap bitmap(FPDFBitmap_Create(kRenderWidth, kRenderHeight, 0));
  if (!bitmap) {
    return false;
  }
  FPDFBitmap_FillRect(bitmap.get(), 0, 0, kRenderWidth, kRenderHeight,
                      0xFFFFFFFF);
  const float width = std::max(FPDF_GetPageWidthF(page), 1.0f);
  const float height = std::max(FPDF_GetPageHeightF(page), 1.0f);
  const float scale = std::min(kRenderWidth / width, kRenderHeight / height);
  const FS_MATRIX matrix = {scale, 0, 0, scale, 0, 0};
  const FS_RECTF clip = {0, 0, static_cast<float>(kRenderWidth),
                         static_cast<float>(kRenderHeight)};
  int status = EPDF_RenderPageBitmapWithMatrix_Start(
      bitmap.get(), page, &matrix, &clip, FPDF_ANNOT, kRenderBudgetMs);
  while (status == FPDF_RENDER_TOBECONTINUED) {
    status = EPDF_RenderPage_Continue(page, kRenderBudgetMs);
  }
  FPDF_RenderPage_Close(page);
  return status == FPDF_RENDER_DONE &&
         FPDFBitmap_GetBuffer(bitmap.get()) != nullptr;
}

bool ReadText(FPDF_PAGE page) {
  ScopedFPDFTextPage text(FPDFText_LoadPage(page));
  if (!text) {
    return false;
  }
  const int count = FPDFText_CountChars(text.get());
  if (count > 0) {
    std::vector<unsigned short> buffer(count + 1);
    FPDFText_GetText(text.get(), 0, count, buffer.data());
  }
  for (int i = 0; i < std::min(count, kGeometryChars); ++i) {
    EPDF_CHAR_GEOMETRY geometry;
    EPDFText_GetCharGeometry(text.get(), i, &geometry);
  }
  return true;
}

void ReadAnnotations(FPDF_PAGE page) {
  const int count = FPDFPage_GetAnnotCount(page);
  for (int i = 0; i < count; ++i) {
    ScopedFPDFAnnotation annot(FPDFPage_GetAnnot(page, i));
    if (annot) {
      FPDFAnnot_GetSubtype(annot.get());
      FS_RECTF rect;
      FPDFAnnot_GetRect(annot.get(), &rect);
    }
  }
}

bool ReadPages(FPDF_DOCUMENT doc, const Options& opts, const Worker& w) {
  const int count = FPDF_GetPageCount(doc);
  for (int i = 0; i < std::min<int>(count, opts.pages); ++i) {
    ScopedFPDFPage page(FPDF_LoadPage(doc, i));
    if (!page) {
      return w.Fail("load page");
    }
    if (opts.render && !RenderPageSliced(page.get())) {
      return w.Fail("render");
    }
    if (!ReadText(page.get())) {
      return w.Fail("text");
    }
    ReadAnnotations(page.get());
  }
  return true;
}

// Reads every field's values; returns the object number of the first text
// field, or 0.
uint32_t ReadForm(FPDF_DOCUMENT doc) {
  EPDF_FORM_MODEL model = EPDFForm_LoadModel(doc);
  if (!model) {
    return 0;
  }
  uint32_t first_text_field = 0;
  const int fields = EPDFForm_CountFields(model);
  for (int f = 0; f < fields; ++f) {
    const int values = EPDFForm_CountFieldValues(model, f);
    for (int v = 0; v < values; ++v) {
      FPDF_WCHAR buffer[256];
      EPDFForm_GetFieldValueAt(model, f, v, buffer, sizeof(buffer));
    }
    if (!first_text_field &&
        EPDFForm_GetFieldFamily(model, f) == EPDF_FORMFIELD_FAMILY_TEXT) {
      first_text_field = EPDFForm_GetFieldObjNum(model, f);
    }
  }
  EPDFForm_CloseModel(model);
  return first_text_field;
}

bool AddSquare(FPDF_PAGE page) {
  ScopedFPDFAnnotation annot(FPDFPage_CreateAnnot(page, FPDF_ANNOT_SQUARE));
  const FS_RECTF rect = {20, 120, 120, 20};
  return annot && FPDFAnnot_SetRect(annot.get(), &rect) &&
         FPDFAnnot_SetColor(annot.get(), FPDFANNOT_COLORTYPE_Color, 200, 30, 30,
                            255) &&
         EPDFAnnot_GenerateAppearance(annot.get());
}

bool AddRichFreeText(FPDF_PAGE page) {
  ScopedFPDFAnnotation annot(FPDFPage_CreateAnnot(page, FPDF_ANNOT_FREETEXT));
  const FS_RECTF rect = {140, 160, 300, 100};
  return annot && FPDFAnnot_SetRect(annot.get(), &rect) &&
         EPDFAnnot_SetRichTextJSON(annot.get(), kRichTextJson);
}

bool WriteTextField(FPDF_DOCUMENT doc, uint32_t field_objnum) {
  const std::vector<unsigned short> text = {'s', 'o', 'a', 'k', 0};
  const FPDF_WIDESTRING values[] = {text.data()};
  if (!EPDFForm_SetFieldValue(doc, field_objnum, EPDF_FORM_VALUE_SCALAR, values,
                              1)) {
    return false;
  }
  uint32_t changed[16];
  unsigned long changed_count = 0;
  return EPDFForm_RedrawFieldWidgets(doc, field_objnum, changed, 16,
                                     &changed_count);
}

bool Redact(FPDF_PAGE page) {
  {
    ScopedFPDFAnnotation annot(FPDFPage_CreateAnnot(page, FPDF_ANNOT_REDACT));
    const FS_RECTF rect = {0, 400, 300, 300};
    if (!annot || !FPDFAnnot_SetRect(annot.get(), &rect)) {
      return false;
    }
  }
  uint32_t removed = 0;
  return EPDFPage_ApplyRedactions(page, &removed);
}

// One action, as the engine runs it: a transaction around the writes.
bool Edit(FPDF_DOCUMENT layer,
          uint32_t text_field,
          const Options& opts,
          const Worker& w) {
  if (!EPDFLayer_BeginTransaction(layer)) {
    return w.Fail("begin transaction");
  }
  bool ok = true;
  {
    ScopedFPDFPage page(FPDF_LoadPage(layer, 0));
    if (!page) {
      ok = w.Fail("load page for edit");
    } else if (!AddSquare(page.get())) {
      ok = w.Fail("add square");
    } else if (!AddRichFreeText(page.get())) {
      ok = w.Fail("add rich FreeText");
    } else if (opts.redact && !Redact(page.get())) {
      ok = w.Fail("redact");
    }
  }
  if (ok && text_field && !WriteTextField(layer, text_field)) {
    ok = w.Fail("write text field");
  }
  if (!ok) {
    EPDFLayer_AbortTransaction(layer);
    return false;
  }
  return EPDFLayer_CommitTransaction(layer) || w.Fail("commit transaction");
}

bool RenderFirstPage(FPDF_DOCUMENT doc) {
  ScopedFPDFPage page(FPDF_LoadPage(doc, 0));
  return page && RenderPageSliced(page.get());
}

bool EncryptAndSave(FPDF_DOCUMENT doc) {
  if (!EPDF_SetEncryption(doc, "user", "owner",
                          EPDF_PERM_PRINT | EPDF_PERM_COPY)) {
    return false;
  }
  unsigned long out_size = 0;
  void* buffer = EPDF_SaveDocumentToOwnedBuffer(doc, 0, &out_size);
  const bool ok = buffer != nullptr && out_size > 0;
  EPDF_FreeBuffer(buffer);
  return ok;
}

// Saves the layer's delta and opens a second layer from it over the same
// base: delta ingest, then a render of what it holds.
bool ReopenFromDelta(EPDF_BASE_DOCUMENT base,
                     FPDF_DOCUMENT layer,
                     const Options& opts,
                     const Worker& w) {
  unsigned long size = 0;
  EPDFLayerSaveStatus status = EPDFLayerSaveStatus_kSaveFailed;
  void* delta = EPDFLayer_SaveDeltaToOwnedBuffer(layer, &size, &status);
  if (!delta && status != EPDFLayerSaveStatus_kSuccess) {
    return w.Fail("save delta");
  }
  // An unchanged layer has an empty delta, and reopens as a fresh layer.
  std::vector<uint8_t> bytes;
  if (delta) {
    bytes.assign(static_cast<uint8_t*>(delta),
                 static_cast<uint8_t*>(delta) + size);
    EPDF_FreeBuffer(delta);
  }
  epdf_layer_tool::MemoryFile file(&bytes);
  ScopedFPDFDocument reopened(EPDFLayer_OpenLayer(
      base, bytes.empty() ? nullptr : file.file_access(), nullptr, nullptr));
  if (!reopened) {
    return w.Fail("reopen layer from delta");
  }
  if (opts.render && !RenderFirstPage(reopened.get())) {
    return w.Fail("render reopened layer");
  }
  return true;
}

bool RunLayerSession(const std::vector<uint8_t>& bytes,
                     const Options& opts,
                     const Worker& w) {
  EPDF_BASE_DOCUMENT base =
      EPDF_LoadMemBaseDocument64(bytes.data(), bytes.size(), nullptr);
  if (!base) {
    return w.Fail("load base");
  }
  bool ok = true;
  {
    EPDFLayerOpenStatus status;
    ScopedFPDFDocument layer(
        EPDFLayer_OpenLayer(base, nullptr, nullptr, &status));
    if (!layer) {
      ok = w.Fail("open layer");
    } else {
      const uint32_t text_field = ReadForm(layer.get());
      ok = ReadPages(layer.get(), opts, w) &&
           (!opts.edit || Edit(layer.get(), text_field, opts, w)) &&
           (!opts.render || RenderFirstPage(layer.get()) ||
            w.Fail("render after edit")) &&
           ReopenFromDelta(base, layer.get(), opts, w) &&
           (!opts.encrypt || EncryptAndSave(layer.get()) ||
            w.Fail("encrypted save"));
    }
  }  // The layer closes before its base is released.
  EPDF_ReleaseBaseDocument(base);
  return ok;
}

bool RunPlainSession(const std::vector<uint8_t>& bytes,
                     const Options& opts,
                     const Worker& w) {
  ScopedFPDFDocument doc(
      FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr));
  if (!doc) {
    return w.Fail("load");
  }
  if (opts.render && !RenderFirstPage(doc.get())) {
    return w.Fail("render");
  }
  if (opts.encrypt && !EncryptAndSave(doc.get())) {
    return w.Fail("encrypted save");
  }
  return true;
}

bool RunWorker(const std::vector<std::string>& paths,
               const std::vector<std::vector<uint8_t>>& files,
               const Options& opts,
               size_t worker_index) {
  for (size_t i = 0; i < opts.iterations; ++i) {
    const size_t pick = (worker_index + i) % files.size();
    const Worker w{worker_index, i, &paths[pick]};
    EPDF_InitThread();
    EPDF_SetDecodedImageBudget(kDecodedImageBudget);
    const bool ok = opts.plain ? RunPlainSession(files[pick], opts, w)
                               : RunLayerSession(files[pick], opts, w);
    // Every handle is closed by now: shut down on the same thread.
    EPDF_ShutdownThread();
    if (!ok) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> paths;
  Options opts;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("--threads=", 0) == 0) {
      opts.threads = ParseSizeArg(arg, "--threads=", opts.threads);
    } else if (arg.rfind("--iterations=", 0) == 0) {
      opts.iterations = ParseSizeArg(arg, "--iterations=", opts.iterations);
    } else if (arg.rfind("--pages=", 0) == 0) {
      opts.pages = ParseSizeArg(arg, "--pages=", opts.pages);
    } else if (arg == "--plain") {
      opts.plain = true;
    } else if (arg == "--no-render") {
      opts.render = false;
    } else if (arg == "--no-encrypt") {
      opts.encrypt = false;
    } else if (arg == "--no-edit") {
      opts.edit = false;
    } else if (arg == "--no-redact") {
      opts.redact = false;
    } else if (arg.rfind("--", 0) == 0) {
      epdf_layer_tool::PrintUsage(argv[0], kUsageExtra);
      return 2;
    } else {
      paths.push_back(arg);
    }
  }

  if (paths.empty()) {
    epdf_layer_tool::PrintUsage(argv[0], kUsageExtra);
    return 2;
  }
  if (opts.threads == 0 || opts.iterations == 0) {
    std::fprintf(stderr, "Thread count and iterations must be positive.\n");
    return 2;
  }

  std::vector<std::vector<uint8_t>> files(paths.size());
  for (size_t i = 0; i < paths.size(); ++i) {
    if (!epdf_layer_tool::ReadFile(paths[i], &files[i])) {
      std::fprintf(stderr, "Failed to read %s\n", paths[i].c_str());
      return 1;
    }
  }

  std::atomic<size_t> failures{0};
  std::vector<std::thread> workers;
  workers.reserve(opts.threads);
  for (size_t t = 0; t < opts.threads; ++t) {
    workers.emplace_back([&paths, &files, &opts, &failures, t]() {
      if (!RunWorker(paths, files, opts, t)) {
        failures.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  const size_t failed = failures.load(std::memory_order_relaxed);
  if (failed != 0) {
    std::fprintf(stderr, "thread soak FAILED: %zu/%zu workers errored\n",
                 failed, opts.threads);
    return 1;
  }

  std::printf(
      "thread soak OK: files=%zu threads=%zu iterations=%zu mode=%s render=%d "
      "encrypt=%d edit=%d redact=%d\n",
      paths.size(), opts.threads, opts.iterations,
      opts.plain ? "plain" : "layer", opts.render ? 1 : 0, opts.encrypt ? 1 : 0,
      opts.edit ? 1 : 0, opts.redact ? 1 : 0);
  return 0;
}
