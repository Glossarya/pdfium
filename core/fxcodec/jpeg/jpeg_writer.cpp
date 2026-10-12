// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fxcodec/jpeg/jpeg_writer.h"

#include <setjmp.h>
#include <stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

#include "core/fxcodec/jpeg/jpeg_common.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/stl_util.h"

namespace fxcodec {

namespace {

constexpr int kBlockSize = 8;
constexpr int kBlockArea = kBlockSize * kBlockSize;

using Block = std::array<double, kBlockArea>;

// libjpeg's error manager. An error jumps back to the last setjmp() on
// `jump`; a warning (a damaged JPEG libjpeg reads on) is noted.
struct ErrorManager {
  jpeg_error_mgr pub;  // First: libjpeg casts to it.
  jmp_buf jump;
  bool warned;
};

void ExitOnError(j_common_ptr cinfo) {
  longjmp(reinterpret_cast<ErrorManager*>(cinfo->err)->jump, 1);
}

void NoteMessage(j_common_ptr cinfo, int level) {
  if (level < 0) {
    reinterpret_cast<ErrorManager*>(cinfo->err)->warned = true;
  }
}

void SilenceOutput(j_common_ptr cinfo) {}

// The libjpeg state of one transcode. A jump out of libjpeg leaves only
// this, which its destructor releases.
struct Transcoder {
  ~Transcoder() {
    if (target_created) {
      jpeg_destroy_compress(&target);
    }
    if (source_created) {
      jpeg_destroy_decompress(&source);
    }
    free(output);
  }

  ErrorManager errors = {};
  jpeg_decompress_struct source = {};
  jpeg_compress_struct target = {};
  bool source_created = false;
  bool target_created = false;
  jvirt_barray_ptr* coefficients = nullptr;
  unsigned char* output = nullptr;
  unsigned long output_size = 0;
};

// basis[u][x] = C(u) / 2 * cos((2x + 1) u pi / 16), C(0) = 1 / sqrt(2), else
// 1: the forward DCT is F(u, v) = sum over x, y of basis[u][x] basis[v][y]
// f(x, y), the inverse the same sum over u, v (ITU-T T.81 A.3.3).
const std::array<std::array<double, kBlockSize>, kBlockSize>& Basis() {
  static const std::array<std::array<double, kBlockSize>, kBlockSize> basis =
      [] {
        std::array<std::array<double, kBlockSize>, kBlockSize> table = {};
        for (int u = 0; u < kBlockSize; ++u) {
          const double scale = u == 0 ? 0.5 / std::sqrt(2.0) : 0.5;
          for (int x = 0; x < kBlockSize; ++x) {
            table[u][x] =
                scale * std::cos((2 * x + 1) * u * 3.14159265358979323846 / 16);
          }
        }
        return table;
      }();
  return basis;
}

// Pixels (row by row) to coefficients (in libjpeg's natural order: vertical
// frequency by horizontal frequency).
Block Forward(const Block& pixels) {
  const auto& basis = Basis();
  Block rows = {};
  for (int v = 0; v < kBlockSize; ++v) {
    for (int x = 0; x < kBlockSize; ++x) {
      double sum = 0;
      for (int y = 0; y < kBlockSize; ++y) {
        sum += basis[v][y] * pixels[y * kBlockSize + x];
      }
      rows[v * kBlockSize + x] = sum;
    }
  }
  Block coefficients = {};
  for (int v = 0; v < kBlockSize; ++v) {
    for (int u = 0; u < kBlockSize; ++u) {
      double sum = 0;
      for (int x = 0; x < kBlockSize; ++x) {
        sum += basis[u][x] * rows[v * kBlockSize + x];
      }
      coefficients[v * kBlockSize + u] = sum;
    }
  }
  return coefficients;
}

Block Inverse(const Block& coefficients) {
  const auto& basis = Basis();
  Block columns = {};
  for (int y = 0; y < kBlockSize; ++y) {
    for (int u = 0; u < kBlockSize; ++u) {
      double sum = 0;
      for (int v = 0; v < kBlockSize; ++v) {
        sum += basis[v][y] * coefficients[v * kBlockSize + u];
      }
      columns[y * kBlockSize + u] = sum;
    }
  }
  Block pixels = {};
  for (int y = 0; y < kBlockSize; ++y) {
    for (int x = 0; x < kBlockSize; ++x) {
      double sum = 0;
      for (int u = 0; u < kBlockSize; ++u) {
        sum += basis[u][x] * columns[y * kBlockSize + u];
      }
      pixels[y * kBlockSize + x] = sum;
    }
  }
  return pixels;
}

JCOEF Quantize(double value, UINT16 step) {
  return static_cast<JCOEF>(
      std::clamp(std::round(value / step), -32767.0, 32767.0));
}

bool Contains(const JpegRect& rect, int x, int y) {
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

bool Overlaps(const JpegRect& a, const JpegRect& b) {
  return a.left < b.right && b.left < a.right && a.top < b.bottom &&
         b.top < a.bottom;
}

// Fills the pixels of `block` (at `x0`, `y0` of a component `width` x
// `height` samples) that `rects` cover with `level`. Pixels past the
// component's edge are never seen, so they count as covered: a block whose
// visible pixels are all covered becomes flat. In a block partly covered,
// the others keep the values a decoder shows, so the block holds nothing of
// what was under the rects.
void FillBlock(pdfium::span<JCOEF, kBlockArea> block,
               const JQUANT_TBL& table,
               const std::vector<JpegRect>& rects,
               int x0,
               int y0,
               int width,
               int height,
               double level) {
  std::array<bool, kBlockArea> covered = {};
  bool all = true;
  for (int y = 0; y < kBlockSize; ++y) {
    for (int x = 0; x < kBlockSize; ++x) {
      const int gx = x0 + x;
      const int gy = y0 + y;
      const bool cover = gx >= width || gy >= height ||
                         std::ranges::any_of(rects, [&](const JpegRect& rect) {
                           return Contains(rect, gx, gy);
                         });
      covered[y * kBlockSize + x] = cover;
      all = all && cover;
    }
  }
  const pdfium::span<const UINT16, kBlockArea> steps(table.quantval);
  // Coefficients are of the level-shifted samples (T.81 A.3.1).
  const double shifted = level - 128;
  if (all) {
    std::ranges::fill(block, 0);
    block[0] = Quantize(kBlockSize * shifted, steps[0]);
    return;
  }
  Block coefficients = {};
  for (int k = 0; k < kBlockArea; ++k) {
    coefficients[k] = static_cast<double>(block[k]) * steps[k];
  }
  Block pixels = Inverse(coefficients);
  for (int p = 0; p < kBlockArea; ++p) {
    pixels[p] = covered[p]
                    ? shifted
                    : std::clamp(std::round(pixels[p] + 128), 0.0, 255.0) - 128;
  }
  coefficients = Forward(pixels);
  for (int k = 0; k < kBlockArea; ++k) {
    block[k] = Quantize(coefficients[k], steps[k]);
  }
}

// `rects` (image pixels) in `component`'s samples: every sample a rect covers
// any part of.
std::vector<JpegRect> ComponentRects(const jpeg_component_info& component,
                                     int max_h,
                                     int max_v,
                                     pdfium::span<const JpegRect> rects) {
  const int h = component.h_samp_factor;
  const int v = component.v_samp_factor;
  const int width = static_cast<int>(component.downsampled_width);
  const int height = static_cast<int>(component.downsampled_height);
  std::vector<JpegRect> result;
  for (const JpegRect& rect : rects) {
    JpegRect local;
    local.left = std::clamp(rect.left * h / max_h, 0, width);
    local.right = std::clamp((rect.right * h + max_h - 1) / max_h, 0, width);
    local.top = std::clamp(rect.top * v / max_v, 0, height);
    local.bottom = std::clamp((rect.bottom * v + max_v - 1) / max_v, 0, height);
    if (local.right > local.left && local.bottom > local.top) {
      result.push_back(local);
    }
  }
  return result;
}

// The values `source` stores for `fill`, given as the decoder hands
// components out: YCbCr for RGB it converts, YCCK (YCbCr of the inverted CMY,
// and K) for CMYK it converts. JpegModule's decoder converts a 3-component
// JPEG only with the colour transform or an Adobe marker.
std::array<double, 4> StoredLevels(const jpeg_decompress_struct& source,
                                   bool color_transform,
                                   pdfium::span<const uint8_t> fill) {
  std::array<double, 4> levels = {};
  for (int c = 0; c < source.num_components; ++c) {
    levels[c] = fill[c];
  }
  const bool ycbcr = source.num_components == 3 &&
                     source.jpeg_color_space == JCS_YCbCr &&
                     (color_transform || source.saw_Adobe_marker);
  const bool ycck =
      source.num_components == 4 && source.jpeg_color_space == JCS_YCCK;
  if (ycbcr || ycck) {
    const double r = ycck ? 255 - levels[0] : levels[0];
    const double g = ycck ? 255 - levels[1] : levels[1];
    const double b = ycck ? 255 - levels[2] : levels[2];
    levels[0] = 0.299 * r + 0.587 * g + 0.114 * b;
    levels[1] = 128 - 0.168736 * r - 0.331264 * g + 0.5 * b;
    levels[2] = 128 + 0.5 * r - 0.418688 * g - 0.081312 * b;
  }
  return levels;
}

// Reads `jpeg`'s header and coefficients into `t`. False on a libjpeg error.
bool ReadCoefficients(Transcoder* t, pdfium::span<const uint8_t> jpeg) {
  if (setjmp(t->errors.jump)) {
    return false;
  }
  jpeg_create_decompress(&t->source);
  t->source_created = true;
  jpeg_mem_src(&t->source, jpeg.data(),
               static_cast<unsigned long>(jpeg.size()));
  if (jpeg_read_header(&t->source, TRUE) != JPEG_HEADER_OK) {
    return false;
  }
  t->coefficients = jpeg_read_coefficients(&t->source);
  return !!t->coefficients;
}

// Fills `component_rects` in `t`'s coefficients and writes them into
// `t->output`. False on a libjpeg error. Creates nothing a jump out of it
// would leave behind.
bool FillAndWrite(Transcoder* t,
                  const std::vector<std::vector<JpegRect>>& component_rects,
                  const std::array<double, 4>& levels) {
  if (setjmp(t->errors.jump)) {
    return false;
  }
  jpeg_decompress_struct& source = t->source;
  for (int c = 0; c < source.num_components; ++c) {
    const std::vector<JpegRect>& rects = component_rects[c];
    if (rects.empty()) {
      continue;
    }
    // SAFETY: libjpeg allocates `num_components` components and arrays.
    const jpeg_component_info& component = UNSAFE_BUFFERS(source.comp_info[c]);
    jvirt_barray_ptr array = UNSAFE_BUFFERS(t->coefficients[c]);
    int top = static_cast<int>(component.downsampled_height);
    int bottom = 0;
    for (const JpegRect& rect : rects) {
      top = std::min(top, rect.top);
      bottom = std::max(bottom, rect.bottom);
    }
    const int rows = std::min(static_cast<int>(component.height_in_blocks),
                              (bottom + kBlockSize - 1) / kBlockSize);
    for (int by = top / kBlockSize; by < rows; ++by) {
      JBLOCKARRAY row = (*source.mem->access_virt_barray)(
          reinterpret_cast<j_common_ptr>(&source), array,
          static_cast<JDIMENSION>(by), 1, TRUE);
      // SAFETY: access_virt_barray() returns one row of `width_in_blocks`
      // blocks.
      pdfium::span<JBLOCK> blocks =
          UNSAFE_BUFFERS(pdfium::span(row[0], component.width_in_blocks));
      for (size_t bx = 0; bx < blocks.size(); ++bx) {
        const int x0 = static_cast<int>(bx) * kBlockSize;
        const int y0 = by * kBlockSize;
        const JpegRect block = {x0, y0, x0 + kBlockSize, y0 + kBlockSize};
        if (std::ranges::none_of(rects, [&](const JpegRect& rect) {
              return Overlaps(rect, block);
            })) {
          continue;
        }
        FillBlock(blocks[bx], *component.quant_table, rects, x0, y0,
                  static_cast<int>(component.downsampled_width),
                  static_cast<int>(component.downsampled_height), levels[c]);
      }
    }
  }

  jpeg_create_compress(&t->target);
  t->target_created = true;
  jpeg_copy_critical_parameters(&source, &t->target);
  // The same colour transform marker as the source, so a decoder reads the
  // components as it read them; no other marker is copied.
  t->target.write_JFIF_header = source.saw_JFIF_marker;
  t->target.write_Adobe_marker = source.saw_Adobe_marker;
  t->target.optimize_coding = TRUE;
  if (source.progressive_mode) {
    jpeg_simple_progression(&t->target);
  }
  jpeg_mem_dest(&t->target, &t->output, &t->output_size);
  jpeg_write_coefficients(&t->target, t->coefficients);
  jpeg_finish_compress(&t->target);
  jpeg_finish_decompress(&source);
  return true;
}

// The libjpeg state of one encode, released as Transcoder's is.
struct Encoder {
  ~Encoder() {
    if (created) {
      jpeg_destroy_compress(&cinfo);
    }
    free(output);
  }

  ErrorManager errors = {};
  jpeg_compress_struct cinfo = {};
  bool created = false;
  unsigned char* output = nullptr;
  unsigned long output_size = 0;
};

// Compresses the rows `row` writes into `e->output`, each through `line` (the
// caller's buffer: nothing with a destructor lives in this frame). False on a
// libjpeg error.
bool Compress(Encoder* e,
              int width,
              int height,
              int components,
              int quality,
              const std::function<void(int, pdfium::span<uint8_t>)>& row,
              pdfium::span<uint8_t> line) {
  if (setjmp(e->errors.jump)) {
    return false;
  }
  jpeg_create_compress(&e->cinfo);
  e->created = true;
  jpeg_mem_dest(&e->cinfo, &e->output, &e->output_size);
  e->cinfo.image_width = static_cast<JDIMENSION>(width);
  e->cinfo.image_height = static_cast<JDIMENSION>(height);
  e->cinfo.input_components = components;
  e->cinfo.in_color_space = components == 1 ? JCS_GRAYSCALE : JCS_RGB;
  jpeg_set_defaults(&e->cinfo);
  jpeg_set_quality(&e->cinfo, quality, TRUE);
  e->cinfo.optimize_coding = TRUE;
  jpeg_start_compress(&e->cinfo, TRUE);
  while (e->cinfo.next_scanline < e->cinfo.image_height) {
    row(static_cast<int>(e->cinfo.next_scanline), line);
    JSAMPROW pointer = line.data();
    jpeg_write_scanlines(&e->cinfo, &pointer, 1);
  }
  jpeg_finish_compress(&e->cinfo);
  return true;
}

}  // namespace

std::optional<DataVector<uint8_t>> FillJpegRects(
    pdfium::span<const uint8_t> jpeg,
    int width,
    int height,
    int components,
    bool color_transform,
    pdfium::span<const JpegRect> rects,
    pdfium::span<const uint8_t> fill) {
  if (jpeg.empty() || components < 1 || components > 4 ||
      fill.size() < static_cast<size_t>(components)) {
    return std::nullopt;
  }
  Transcoder t;
  t.source.err = jpeg_std_error(&t.errors.pub);
  t.target.err = &t.errors.pub;
  t.errors.pub.error_exit = ExitOnError;
  t.errors.pub.emit_message = NoteMessage;
  t.errors.pub.output_message = SilenceOutput;

  if (!ReadCoefficients(&t, jpeg) || t.errors.warned ||
      static_cast<int>(t.source.image_width) != width ||
      static_cast<int>(t.source.image_height) != height ||
      t.source.num_components != components || t.source.data_precision != 8) {
    return std::nullopt;
  }
  std::vector<std::vector<JpegRect>> component_rects;
  for (int c = 0; c < components; ++c) {
    // SAFETY: libjpeg allocates `num_components` components.
    const jpeg_component_info& component =
        UNSAFE_BUFFERS(t.source.comp_info[c]);
    if (!component.quant_table) {
      return std::nullopt;
    }
    component_rects.push_back(
        ComponentRects(component, t.source.max_h_samp_factor,
                       t.source.max_v_samp_factor, rects));
  }
  const std::array<double, 4> levels =
      StoredLevels(t.source, color_transform, fill);
  if (!FillAndWrite(&t, component_rects, levels) || t.errors.warned) {
    return std::nullopt;
  }
  // SAFETY: jpeg_mem_dest() wrote `output_size` bytes at `output`.
  pdfium::span<const uint8_t> written =
      UNSAFE_BUFFERS(pdfium::span(t.output, t.output_size));
  return DataVector<uint8_t>(written.begin(), written.end());
}

std::optional<DataVector<uint8_t>> EncodeJpeg(
    int width,
    int height,
    int components,
    int quality,
    const std::function<void(int y, pdfium::span<uint8_t> out)>& row) {
  if (width <= 0 || height <= 0 || (components != 1 && components != 3) ||
      quality < 1 || quality > 100) {
    return std::nullopt;
  }
  Encoder e;
  e.cinfo.err = jpeg_std_error(&e.errors.pub);
  e.errors.pub.error_exit = ExitOnError;
  e.errors.pub.emit_message = NoteMessage;
  e.errors.pub.output_message = SilenceOutput;
  DataVector<uint8_t> line(static_cast<size_t>(width) * components);
  if (!Compress(&e, width, height, components, quality, row, line)) {
    return std::nullopt;
  }
  // SAFETY: jpeg_mem_dest() wrote `output_size` bytes at `output`.
  pdfium::span<const uint8_t> written =
      UNSAFE_BUFFERS(pdfium::span(e.output, e.output_size));
  return DataVector<uint8_t>(written.begin(), written.end());
}

}  // namespace fxcodec
