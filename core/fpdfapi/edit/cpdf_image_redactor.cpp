// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/edit/cpdf_image_redactor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include "core/fpdfapi/page/cpdf_colorspace.h"
#include "core/fpdfapi/page/cpdf_dib.h"
#include "core/fpdfapi/page/cpdf_docpagedata.h"
#include "core/fpdfapi/page/cpdf_image.h"
#include "core/fpdfapi/page/cpdf_imageobject.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_boolean.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "core/fxcodec/flate/flatemodule.h"
#include "core/fxcodec/jpeg/jpeg_coefficients.h"
#include "core/fxcodec/jpeg/jpegmodule.h"
#include "core/fxcodec/scanlinedecoder.h"
#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/fx_safe_types.h"
#include "core/fxcrt/span.h"
#include "core/fxcrt/stl_util.h"
#include "core/fxge/dib/cfx_dibitmap.h"
#include "core/fxge/dib/fx_dib.h"

namespace {

// -- Grids --------------------------------------------------------------------

// Maps page-space rects into the image's sample grid (image-local).
static void PageRectsToImageGrid(const CFX_Matrix& image_to_page,
                                 int img_w,
                                 int img_h,
                                 pdfium::span<const CFX_FloatRect> page_rects,
                                 std::vector<CFX_FloatRect>* out_image_rects) {
  out_image_rects->clear();
  if (img_w <= 0 || img_h <= 0 || page_rects.empty()) {
    return;
  }

  // Step 1: page -> unit image space
  const CFX_Matrix page_to_unit = image_to_page.GetInverse();

  out_image_rects->reserve(page_rects.size());
  for (const auto& pr : page_rects) {
    // Page -> unit
    CFX_FloatRect ur = page_to_unit.TransformRect(pr);
    ur.Normalize();

    // Step 2: unit -> pixel
    CFX_FloatRect ir(ur.left * img_w, ur.bottom * img_h, ur.right * img_w,
                     ur.top * img_h);
    ir.Normalize();

    // Clamp to pixel bounds
    ir.left = std::clamp(ir.left, 0.0f, static_cast<float>(img_w));
    ir.right = std::clamp(ir.right, 0.0f, static_cast<float>(img_w));
    ir.bottom = std::clamp(ir.bottom, 0.0f, static_cast<float>(img_h));
    ir.top = std::clamp(ir.top, 0.0f, static_cast<float>(img_h));

    if (ir.right > ir.left && ir.top > ir.bottom) {
      out_image_rects->push_back(ir);
    }
  }
}

// EmbedPDF: the samples of an image grid the rects (from
// PageRectsToImageGrid()) cover any part of, as whole-sample boxes. Rows
// count from the bottom; x1 and y1 are one past the box.
struct GridBox {
  int x0, y0, x1, y1;
};

static std::vector<GridBox> GridBoxesOf(
    pdfium::span<const CFX_FloatRect> grid_rects,
    int width,
    int height) {
  std::vector<GridBox> boxes;
  boxes.reserve(grid_rects.size());
  for (const CFX_FloatRect& r : grid_rects) {
    GridBox b;
    b.x0 = std::max(0, std::min(width, static_cast<int>(std::floor(r.left))));
    b.x1 = std::max(0, std::min(width, static_cast<int>(std::ceil(r.right))));
    b.y0 =
        std::max(0, std::min(height, static_cast<int>(std::floor(r.bottom))));
    b.y1 = std::max(0, std::min(height, static_cast<int>(std::ceil(r.top))));
    if (b.x1 > b.x0 && b.y1 > b.y0) {
      boxes.push_back(b);
    }
  }
  return boxes;
}

// EmbedPDF: a private copy of an image's explicit mask (an image mask of its
// own, ISO 32000-1 §8.9.6.3, with its own grid over the same unit square),
// painted where `page_rects` cover it: the hole shows as the replacement
// image's fill, as the soft-mask path makes it opaque. Null when the mask
// can't be read.
static RetainPtr<CPDF_Stream> RedactExplicitMask(
    CPDF_Document* doc,
    RetainPtr<const CPDF_Stream> mask,
    const CFX_Matrix& image_to_page,
    pdfium::span<const CFX_FloatRect> page_rects) {
  auto dib = pdfium::MakeRetain<CPDF_DIB>(doc, std::move(mask));
  if (!dib->Load()) {
    return nullptr;
  }
  CPDF_DIB::LoadState state = CPDF_DIB::LoadState::kSuccess;
  if (dib->IsJBigImage()) {
    do {
      state = dib->ContinueLoadDIBBase(nullptr);
    } while (state == CPDF_DIB::LoadState::kContinue);
  }
  if (state != CPDF_DIB::LoadState::kSuccess || !dib->IsMaskFormat() ||
      dib->GetBPP() != 1) {
    return nullptr;
  }

  const int width = dib->GetWidth();
  const int height = dib->GetHeight();
  std::vector<CFX_FloatRect> grid_rects;
  PageRectsToImageGrid(image_to_page, width, height, page_rects, &grid_rects);
  const std::vector<GridBox> boxes = GridBoxesOf(grid_rects, width, height);

  // CPDF_DIB reads a mask whatever its /Decode with 1 as painted; the copy
  // keeps that and says so with /Decode [1 0].
  const size_t pitch = (static_cast<size_t>(width) + 7) / 8;
  DataVector<uint8_t> bits(pitch * static_cast<size_t>(height));
  for (int row = 0; row < height; ++row) {
    pdfium::span<uint8_t> out =
        pdfium::span(bits).subspan(static_cast<size_t>(row) * pitch, pitch);
    pdfium::span<const uint8_t> line = dib->GetScanline(row);
    if (line.size() >= pitch) {
      fxcrt::Copy(line.first(pitch), out);
    }
    const int y = height - 1 - row;
    for (const GridBox& box : boxes) {
      if (y < box.y0 || y >= box.y1) {
        continue;
      }
      for (int x = box.x0; x < box.x1; ++x) {
        out[x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
      }
    }
  }

  RetainPtr<CPDF_Dictionary> dict = doc->New<CPDF_Dictionary>();
  dict->SetNewFor<CPDF_Name>("Type", "XObject");
  dict->SetNewFor<CPDF_Name>("Subtype", "Image");
  dict->SetNewFor<CPDF_Number>("Width", width);
  dict->SetNewFor<CPDF_Number>("Height", height);
  dict->SetNewFor<CPDF_Boolean>("ImageMask", true);
  dict->SetNewFor<CPDF_Number>("BitsPerComponent", 1);
  RetainPtr<CPDF_Array> decode = dict->SetNewFor<CPDF_Array>("Decode");
  decode->AppendNew<CPDF_Number>(1);
  decode->AppendNew<CPDF_Number>(0);
  dict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");
  auto stream = pdfium::MakeRetain<CPDF_Stream>(
      fxcodec::FlateModule::Encode(bits), std::move(dict));
  if (doc->AddIndirectObject(stream) == 0) {
    return nullptr;
  }
  return stream;
}

// Helper: Manually decode a JPEG stream for SMask.
// Returns true on success and populates out_data.
static bool DecodeJpegSMask(RetainPtr<const CPDF_Stream> stream,
                            int width,
                            int height,
                            DataVector<uint8_t>& out_data) {
  if (!stream) {
    return false;
  }

  // Get raw stream data
  pdfium::span<const uint8_t> raw_span;
  DataVector<uint8_t> raw_data_storage;
  if (stream->IsMemoryBased()) {
    raw_span = stream->GetInMemoryRawData();
  } else {
    raw_data_storage = const_cast<CPDF_Stream*>(stream.Get())->ReadAllRawData();
    raw_span = pdfium::span<const uint8_t>(raw_data_storage);
  }

  if (raw_span.size() < 2) {
    return false;
  }

  // Check for JPEG header
  if (raw_span[0] != 0xFF || raw_span[1] != 0xD8) {
    return false;
  }

  // Get SMask dimensions
  RetainPtr<const CPDF_Dictionary> smask_dict = stream->GetDict();
  int smask_w = smask_dict ? smask_dict->GetIntegerFor("Width") : width;
  int smask_h = smask_dict ? smask_dict->GetIntegerFor("Height") : height;

  // Create JPEG decoder
  auto jpeg_decoder =
      fxcodec::JpegModule::CreateDecoder(raw_span, smask_w, smask_h, 1, false);

  if (!jpeg_decoder) {
    return false;
  }

  out_data.resize(static_cast<size_t>(width) * height);

  // Decode scanlines
  for (int row = 0; row < smask_h && row < height; ++row) {
    pdfium::span<const uint8_t> scanline = jpeg_decoder->GetScanline(row);
    if (!scanline.empty()) {
      size_t copy_len =
          std::min<size_t>(scanline.size(), static_cast<size_t>(width));
      memcpy(out_data.data() + row * width, scanline.data(), copy_len);
    }
  }

  return true;
}

// Whether sample (x, y) of a grid lies in one of `boxes`.
bool InAnyBox(int x, int y, pdfium::span<const GridBox> boxes) {
  for (const GridBox& box : boxes) {
    if (x >= box.x0 && x < box.x1 && y >= box.y0 && y < box.y1) {
      return true;
    }
  }
  return false;
}

// -- Format kept --------------------------------------------------------------

// What a redacted sample becomes.
enum class Hole {
  kPaper,      // An image's: its colour space's paper (white, no ink).
  kUnpainted,  // A stencil image's: nothing painted.
  kPainted,    // An explicit mask's: painted, so the image's hole shows.
  kOpaque,     // A soft mask's: fully opaque, as an image's hole is.
};

// An image's samples as its filters leave them, before its colour space
// reads them: `height` rows of `pitch` bytes, the first row the top one.
struct Samples {
  int width = 0;
  int height = 0;
  int components = 0;
  int bpc = 0;
  size_t pitch = 0;
  DataVector<uint8_t> data;
};

bool IsSampleDepth(int bpc) {
  return bpc == 1 || bpc == 2 || bpc == 4 || bpc == 8 || bpc == 16;
}

// Whether the image reads its samples inverted from how CPDF_DIB hands out
// 1-bit rows: a stencil read with the default /Decode, which CPDF_DIB inverts
// so that 1 is painted.
bool DibInvertsRows(const CPDF_Dictionary& dict) {
  const bool stencil =
      dict.GetBooleanFor("ImageMask", false) || !dict.KeyExist("ColorSpace");
  RetainPtr<const CPDF_Array> decode = dict.GetArrayFor("Decode");
  return stencil && (!decode || !decode->GetIntegerAt(0));
}

// `stream`'s samples, `acc` its data through every filter that leaves
// samples; nullopt when its filters don't leave plain samples (JPEG, JPEG
// 2000) or they can't be read.
std::optional<Samples> DecodeSamples(CPDF_Document* doc,
                                     RetainPtr<const CPDF_Stream> stream,
                                     const CPDF_StreamAcc& acc,
                                     int components,
                                     int bpc) {
  RetainPtr<const CPDF_Dictionary> dict = stream->GetDict();
  Samples samples;
  samples.width = dict->GetIntegerFor("Width");
  samples.height = dict->GetIntegerFor("Height");
  samples.components = components;
  samples.bpc = bpc;
  if (samples.width <= 0 || samples.height <= 0 || components <= 0 ||
      !IsSampleDepth(bpc)) {
    return std::nullopt;
  }
  FX_SAFE_SIZE_T pitch = samples.width;
  pitch *= components;
  pitch *= bpc;
  pitch += 7;
  pitch /= 8;
  FX_SAFE_SIZE_T size = pitch;
  size *= samples.height;
  if (!size.IsValid()) {
    return std::nullopt;
  }
  samples.pitch = pitch.ValueOrDie();
  samples.data.resize(size.ValueOrDie());

  const ByteString decoder = acc.GetImageDecoder();
  if (decoder.IsEmpty()) {
    // Short data reads as zeros, as CPDF_DIB reads it.
    pdfium::span<const uint8_t> data = acc.GetSpan();
    const size_t n = std::min(data.size(), samples.data.size());
    fxcrt::Copy(data.first(n), samples.data);
    return samples;
  }
  if ((decoder != "CCITTFaxDecode" && decoder != "JBIG2Decode") ||
      components * bpc != 1) {
    return std::nullopt;
  }
  // These two decode only through CPDF_DIB (JBIG2 with its globals), whose
  // 1-bit rows are the samples.
  auto dib = pdfium::MakeRetain<CPDF_DIB>(doc, stream);
  if (!dib->Load()) {
    return std::nullopt;
  }
  CPDF_DIB::LoadState state = CPDF_DIB::LoadState::kSuccess;
  if (dib->IsJBigImage()) {
    do {
      state = dib->ContinueLoadDIBBase(nullptr);
    } while (state == CPDF_DIB::LoadState::kContinue);
  }
  if (state != CPDF_DIB::LoadState::kSuccess || dib->GetBPP() != 1 ||
      dib->GetWidth() != samples.width || dib->GetHeight() != samples.height) {
    return std::nullopt;
  }
  const uint8_t flip = DibInvertsRows(*dict) ? 0xFF : 0x00;
  for (int row = 0; row < samples.height; ++row) {
    pdfium::span<uint8_t> out =
        pdfium::span(samples.data)
            .subspan(static_cast<size_t>(row) * samples.pitch, samples.pitch);
    pdfium::span<const uint8_t> line = dib->GetScanline(row);
    if (line.size() < samples.pitch) {
      return std::nullopt;
    }
    for (size_t i = 0; i < samples.pitch; ++i) {
      out[i] = line[i] ^ flip;
    }
  }
  return samples;
}

// The range component `index` of `dict`'s samples decodes to: its /Decode
// pair, else the colour space's default (an index for Indexed).
std::pair<float, float> DecodeRange(const CPDF_Dictionary& dict,
                                    const CPDF_ColorSpace* color_space,
                                    int index,
                                    int components,
                                    uint32_t max_sample) {
  RetainPtr<const CPDF_Array> decode = dict.GetArrayFor("Decode");
  if (decode && decode->size() >= static_cast<size_t>(components) * 2) {
    return {decode->GetFloatAt(index * 2), decode->GetFloatAt(index * 2 + 1)};
  }
  if (!color_space) {
    return {0.0f, 1.0f};
  }
  if (color_space->GetFamily() == CPDF_ColorSpace::Family::kIndexed) {
    return {0.0f, static_cast<float>(max_sample)};
  }
  float value = 0.0f;
  float min = 0.0f;
  float max = 1.0f;
  color_space->GetDefaultValue(index, &value, &min, &max);
  return {min, max};
}

uint32_t SampleFor(float value,
                   std::pair<float, float> range,
                   uint32_t max_sample) {
  const float span = range.second - range.first;
  if (span == 0.0f) {
    return 0;
  }
  const float scaled = std::round((value - range.first) / span * max_sample);
  return static_cast<uint32_t>(
      std::clamp(scaled, 0.0f, static_cast<float>(max_sample)));
}

float Lightness(const CPDF_ColorSpace* color_space,
                pdfium::span<const float> values) {
  const FX_RGB_STRUCT<float> rgb = color_space->GetRGBOrZerosOnError(values);
  return rgb.red + rgb.green + rgb.blue;
}

// The decoded values of `color_space`'s paper: white, or no ink. Nullopt for
// a colour space an image can't use.
std::optional<std::vector<float>> PaperValues(
    const CPDF_Dictionary& dict,
    const CPDF_ColorSpace* color_space,
    int components,
    uint32_t max_sample) {
  using Family = CPDF_ColorSpace::Family;
  switch (color_space->GetFamily()) {
    case Family::kDeviceGray:
    case Family::kCalGray:
    case Family::kDeviceRGB:
    case Family::kCalRGB:
      return std::vector<float>(components, 1.0f);
    case Family::kDeviceCMYK:
    case Family::kSeparation:
    case Family::kDeviceN:
      return std::vector<float>(components, 0.0f);
    case Family::kLab:
      return std::vector<float>{100.0f, 0.0f, 0.0f};
    case Family::kICCBased: {
      // A profile can be gray, RGB, CMYK or Lab: the lighter of its range's
      // two ends.
      std::vector<float> low;
      std::vector<float> high;
      for (int i = 0; i < components; ++i) {
        const auto range =
            DecodeRange(dict, color_space, i, components, max_sample);
        low.push_back(range.first);
        high.push_back(range.second);
      }
      return Lightness(color_space, high) >= Lightness(color_space, low) ? high
                                                                         : low;
    }
    case Family::kIndexed: {
      // The lightest entry.
      float best = -1.0f;
      float best_index = 0.0f;
      for (uint32_t index = 0; index <= max_sample; ++index) {
        const float value = static_cast<float>(index);
        const float lightness =
            Lightness(color_space, pdfium::span_from_ref(value));
        if (lightness > best) {
          best = lightness;
          best_index = value;
        }
      }
      return std::vector<float>{best_index};
    }
    case Family::kPattern:
    case Family::kUnknown:
      return std::nullopt;
  }
  return std::nullopt;
}

// The sample, per component, a hole of `hole` holds in `dict`'s image.
std::optional<std::vector<uint32_t>> HoleSamples(
    Hole hole,
    const CPDF_Dictionary& dict,
    const CPDF_ColorSpace* color_space,
    int components,
    int bpc) {
  const uint32_t max_sample = (1u << bpc) - 1;
  if (hole == Hole::kPainted || hole == Hole::kUnpainted) {
    // A stencil paints where its sample decodes to 0 (ISO 32000-1 §8.9.6.2).
    RetainPtr<const CPDF_Array> decode = dict.GetArrayFor("Decode");
    const bool inverted = decode && decode->GetIntegerAt(0);
    const bool painted = hole == Hole::kPainted;
    return std::vector<uint32_t>{painted != inverted ? 0u : 1u};
  }
  std::vector<float> values;
  if (hole == Hole::kOpaque) {
    values.assign(components, 1.0f);
  } else {
    std::optional<std::vector<float>> paper =
        PaperValues(dict, color_space, components, max_sample);
    if (!paper.has_value() ||
        paper->size() != static_cast<size_t>(components)) {
      return std::nullopt;
    }
    values = std::move(paper.value());
  }
  std::vector<uint32_t> samples;
  for (int i = 0; i < components; ++i) {
    samples.push_back(SampleFor(
        values[i], DecodeRange(dict, color_space, i, components, max_sample),
        max_sample));
  }
  return samples;
}

void SetSample(pdfium::span<uint8_t> row,
               size_t index,
               int bpc,
               uint32_t value) {
  if (bpc == 8) {
    row[index] = static_cast<uint8_t>(value);
    return;
  }
  if (bpc == 16) {
    row[index * 2] = static_cast<uint8_t>(value >> 8);
    row[index * 2 + 1] = static_cast<uint8_t>(value);
    return;
  }
  const size_t bit = index * bpc;
  const int shift = 8 - bpc - static_cast<int>(bit % 8);
  const uint8_t mask = static_cast<uint8_t>(((1u << bpc) - 1) << shift);
  uint8_t& byte = row[bit / 8];
  byte = static_cast<uint8_t>((byte & ~mask) | ((value << shift) & mask));
}

// Sets every sample in `boxes` to `hole`.
void FillHole(Samples& samples,
              pdfium::span<const GridBox> boxes,
              pdfium::span<const uint32_t> hole) {
  for (const GridBox& box : boxes) {
    for (int y = box.y0; y < box.y1; ++y) {
      const size_t row = static_cast<size_t>(samples.height - 1 - y);
      pdfium::span<uint8_t> line =
          pdfium::span(samples.data)
              .subspan(row * samples.pitch, samples.pitch);
      for (int x = box.x0; x < box.x1; ++x) {
        for (int c = 0; c < samples.components; ++c) {
          SetSample(line, static_cast<size_t>(x) * samples.components + c,
                    samples.bpc, hole[c]);
        }
      }
    }
  }
}

// An image's redacted content, ready to be written: its samples, written
// with Flate, or its JPEG, written as is.
struct Content {
  int width = 0;
  int height = 0;
  std::optional<Samples> samples;
  DataVector<uint8_t> jpeg;
  // The JPEG's /ColorTransform, when the image's /DCTDecode had one.
  std::optional<int> color_transform;
};

// A new image stream holding `content`, with `keys` copied from `source` (and
// nothing else of it).
RetainPtr<CPDF_Stream> WriteContent(CPDF_Document* doc,
                                    const CPDF_Dictionary& source,
                                    pdfium::span<const char* const> keys,
                                    const Content& content) {
  RetainPtr<CPDF_Dictionary> dict = doc->New<CPDF_Dictionary>();
  dict->SetNewFor<CPDF_Name>("Type", "XObject");
  dict->SetNewFor<CPDF_Name>("Subtype", "Image");
  dict->SetNewFor<CPDF_Number>("Width", content.width);
  dict->SetNewFor<CPDF_Number>("Height", content.height);
  for (const char* key : keys) {
    if (RetainPtr<const CPDF_Object> value = source.GetObjectFor(key)) {
      dict->SetFor(key, value->CloneForHolder(doc));
    }
  }
  DataVector<uint8_t> data;
  if (content.samples.has_value()) {
    dict->SetNewFor<CPDF_Name>("Filter", "FlateDecode");
    data = fxcodec::FlateModule::Encode(content.samples->data);
  } else {
    dict->SetNewFor<CPDF_Name>("Filter", "DCTDecode");
    if (content.color_transform.has_value()) {
      dict->SetNewFor<CPDF_Dictionary>("DecodeParms")
          ->SetNewFor<CPDF_Number>("ColorTransform",
                                   content.color_transform.value());
    }
    data = content.jpeg;
  }
  auto stream =
      pdfium::MakeRetain<CPDF_Stream>(std::move(data), std::move(dict));
  if (doc->AddIndirectObject(stream) == 0) {
    return nullptr;
  }
  return stream;
}

// What a replacement keeps of the image, a soft mask and a mask: what says
// how, where and when it is drawn. Metadata, alternate versions and OPI
// links are other copies of the pixels; /Filter and /DecodeParms belong to
// the old bytes.
constexpr const char* kImageKeys[] = {
    "ColorSpace", "BitsPerComponent", "Decode", "ImageMask",
    "Intent",     "Interpolate",      "OC",     "StructParent"};
constexpr const char* kSoftMaskKeys[] = {"ColorSpace", "BitsPerComponent",
                                         "Decode", "Matte", "Interpolate"};
constexpr const char* kMaskKeys[] = {"ImageMask", "BitsPerComponent", "Decode",
                                     "Interpolate"};

// `dict`'s image grid boxes under `page_rects`; `image_to_page` places the
// unit square the image fills.
std::vector<GridBox> HoleBoxes(int width,
                               int height,
                               const CFX_Matrix& image_to_page,
                               pdfium::span<const CFX_FloatRect> page_rects) {
  std::vector<CFX_FloatRect> grid_rects;
  PageRectsToImageGrid(image_to_page, width, height, page_rects, &grid_rects);
  return GridBoxesOf(grid_rects, width, height);
}

// A JPEG's content with the hole filled in its blocks (FillJpegRects()):
// the blocks away from the hole are its own, the size about the same.
std::optional<Content> RedactedJpeg(
    const CPDF_StreamAcc& acc,
    const CPDF_Dictionary& dict,
    const CPDF_ColorSpace* color_space,
    Hole hole,
    const CFX_Matrix& image_to_page,
    pdfium::span<const CFX_FloatRect> page_rects) {
  Content content;
  content.width = dict.GetIntegerFor("Width");
  content.height = dict.GetIntegerFor("Height");
  if (!color_space || dict.GetIntegerFor("BitsPerComponent", 8) != 8) {
    return std::nullopt;
  }
  const int components = color_space->ComponentCount();
  std::optional<std::vector<uint32_t>> fill =
      HoleSamples(hole, dict, color_space, components, 8);
  if (!fill.has_value()) {
    return std::nullopt;
  }
  std::vector<uint8_t> fill_bytes(fill->begin(), fill->end());
  // Grid boxes count rows from the bottom; a JPEG's from the top.
  std::vector<fxcodec::JpegRect> rects;
  for (const GridBox& box :
       HoleBoxes(content.width, content.height, image_to_page, page_rects)) {
    rects.push_back({.left = box.x0,
                     .top = content.height - box.y1,
                     .right = box.x1,
                     .bottom = content.height - box.y0});
  }
  // The colour transform as CPDF_DIB reads it.
  RetainPtr<const CPDF_Dictionary> params = acc.GetImageParam();
  const bool color_transform =
      !params || params->GetIntegerFor("ColorTransform", 1);
  std::optional<DataVector<uint8_t>> jpeg =
      fxcodec::FillJpegRects(acc.GetSpan(), content.width, content.height,
                             components, color_transform, rects, fill_bytes);
  if (!jpeg.has_value()) {
    return std::nullopt;
  }
  content.jpeg = std::move(jpeg.value());
  if (params && params->KeyExist("ColorTransform")) {
    content.color_transform = params->GetIntegerFor("ColorTransform");
  }
  return content;
}

// `stream`'s content (the image's, its soft mask's or its mask's) with what
// lies under `page_rects` set to `hole`: a JPEG stays a JPEG, other images
// keep their samples. `image_to_page` places the unit square every one of
// them fills. Nullopt when it can't be kept.
std::optional<Content> RedactedContent(
    CPDF_Document* doc,
    RetainPtr<const CPDF_Stream> stream,
    const CPDF_ColorSpace* color_space,
    Hole hole,
    const CFX_Matrix& image_to_page,
    pdfium::span<const CFX_FloatRect> page_rects) {
  RetainPtr<const CPDF_Dictionary> dict = stream->GetDict();
  auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(stream);
  acc->LoadAllDataFiltered();
  if (acc->GetImageDecoder() == "DCTDecode") {
    return RedactedJpeg(*acc, *dict, color_space, hole, image_to_page,
                        page_rects);
  }
  const bool stencil = hole == Hole::kPainted || hole == Hole::kUnpainted;
  const int components = stencil ? 1 : color_space->ComponentCount();
  const int bpc = stencil ? 1 : dict->GetIntegerFor("BitsPerComponent");
  std::optional<Samples> samples =
      DecodeSamples(doc, stream, *acc, components, bpc);
  if (!samples.has_value()) {
    return std::nullopt;
  }
  std::optional<std::vector<uint32_t>> fill =
      HoleSamples(hole, *dict, color_space, components, bpc);
  if (!fill.has_value()) {
    return std::nullopt;
  }
  FillHole(
      samples.value(),
      HoleBoxes(samples->width, samples->height, image_to_page, page_rects),
      fill.value());
  Content content;
  content.width = samples->width;
  content.height = samples->height;
  content.samples = std::move(samples);
  return content;
}

// The colour space `dict`'s /ColorSpace names, read as CPDF_DIB reads it.
RetainPtr<CPDF_ColorSpace> ColorSpaceOf(CPDF_Document* doc,
                                        const CPDF_Dictionary& dict,
                                        const CPDF_Dictionary* resources,
                                        const CPDF_Dictionary* page_resources) {
  RetainPtr<const CPDF_Object> object = dict.GetDirectObjectFor("ColorSpace");
  if (!object) {
    return nullptr;
  }
  auto* page_data = CPDF_DocPageData::FromDocument(doc);
  RetainPtr<CPDF_ColorSpace> color_space =
      resources ? page_data->GetColorSpace(object.Get(), resources) : nullptr;
  return color_space ? color_space
                     : page_data->GetColorSpace(object.Get(), page_resources);
}

// Redacts `image_object` keeping its format (see RedactImageObject()), or
// nullopt when it can't be kept: then the image is rewritten as RGB.
std::optional<RedactResult> RedactKeepingFormat(
    CPDF_Page* page,
    CPDF_ImageObject* image_object,
    const CPDF_Dictionary* resources,
    pdfium::span<const CFX_FloatRect> page_rects,
    const CFX_Matrix& image_to_page) {
  CPDF_Image* image = image_object->GetImage();
  CPDF_Document* doc = page->GetDocument();
  RetainPtr<const CPDF_Stream> stream = image ? image->GetStream() : nullptr;
  if (!stream || !doc) {
    return std::nullopt;
  }
  RetainPtr<const CPDF_Dictionary> dict = stream->GetDict();
  RetainPtr<const CPDF_Dictionary> page_resources = page->GetResources();

  const bool stencil = image->IsMask();
  RetainPtr<CPDF_ColorSpace> color_space =
      stencil ? nullptr
              : ColorSpaceOf(doc, *dict, resources, page_resources.Get());
  if (!stencil && !color_space) {
    return std::nullopt;
  }

  // Nothing of it under the regions: unchanged.
  std::vector<CFX_FloatRect> grid_rects;
  PageRectsToImageGrid(image_to_page, image->GetPixelWidth(),
                       image->GetPixelHeight(), page_rects, &grid_rects);
  if (GridBoxesOf(grid_rects, image->GetPixelWidth(), image->GetPixelHeight())
          .empty()) {
    return RedactResult();
  }

  // Its masks are kept the same way, or the image is not.
  RetainPtr<const CPDF_Stream> soft_mask = dict->GetStreamFor("SMask");
  RetainPtr<CPDF_ColorSpace> soft_mask_space;
  if (soft_mask) {
    soft_mask_space = ColorSpaceOf(doc, *soft_mask->GetDict(), resources,
                                   page_resources.Get());
    if (!soft_mask_space || soft_mask_space->ComponentCount() != 1) {
      return std::nullopt;
    }
  }
  RetainPtr<const CPDF_Stream> mask =
      soft_mask ? nullptr : dict->GetStreamFor("Mask");

  // Everything is read before anything is written.
  std::optional<Content> content = RedactedContent(
      doc, stream, color_space.Get(), stencil ? Hole::kUnpainted : Hole::kPaper,
      image_to_page, page_rects);
  std::optional<Content> soft_mask_content;
  if (soft_mask) {
    soft_mask_content =
        RedactedContent(doc, soft_mask, soft_mask_space.Get(), Hole::kOpaque,
                        image_to_page, page_rects);
  }
  std::optional<Content> mask_content;
  if (mask) {
    mask_content = RedactedContent(doc, mask, nullptr, Hole::kPainted,
                                   image_to_page, page_rects);
  }
  if (!content.has_value() || (soft_mask && !soft_mask_content.has_value()) ||
      (mask && !mask_content.has_value())) {
    return std::nullopt;
  }

  RetainPtr<CPDF_Stream> replacement =
      WriteContent(doc, *dict, kImageKeys, content.value());
  if (!replacement) {
    return RedactResult{.succeeded = false};
  }
  RetainPtr<CPDF_Dictionary> replacement_dict = replacement->GetMutableDict();
  if (soft_mask) {
    RetainPtr<CPDF_Stream> copy = WriteContent(
        doc, *soft_mask->GetDict(), kSoftMaskKeys, soft_mask_content.value());
    if (!copy) {
      return RedactResult{.succeeded = false};
    }
    replacement_dict->SetNewFor<CPDF_Reference>("SMask", doc,
                                                copy->GetObjNum());
  } else if (mask) {
    RetainPtr<CPDF_Stream> copy =
        WriteContent(doc, *mask->GetDict(), kMaskKeys, mask_content.value());
    if (!copy) {
      return RedactResult{.succeeded = false};
    }
    replacement_dict->SetNewFor<CPDF_Reference>("Mask", doc, copy->GetObjNum());
  } else if (RetainPtr<const CPDF_Array> color_key =
                 dict->GetArrayFor("Mask")) {
    // Colour-key ranges are sample values, which the samples keep.
    replacement_dict->SetFor("Mask", color_key->CloneForHolder(doc));
  }

  // Images (and their masks) may be reused by other placements or pages:
  // only this placement draws the replacement.
  image_object->SetImage(
      pdfium::MakeRetain<CPDF_Image>(doc, replacement->GetObjNum()));
  page->ClearRenderContext();
  image_object->SetDirty(true);
  return RedactResult{.changed = true};
}

// -- Decoded to RGB -----------------------------------------------------------

// Redacts `iobj` decoded to RGB (see RedactImageObject()).
static RedactResult RedactDecodedImage(
    CPDF_Page* page,
    CPDF_ImageObject* iobj,
    pdfium::span<const CFX_FloatRect> page_rects,
    const CFX_Matrix& parent_to_page,
    bool fill_black) {
  if (!page || !iobj) {
    return {.succeeded = false};
  }
  // Object -> page for this placement.
  // Order matters: apply image's internal matrix first, THEN the form
  // placement.
  const CFX_Matrix img_to_page = iobj->matrix() * parent_to_page;

  // Quick reject using unit bbox in page space.
  const CFX_FloatRect img_bbox_page =
      img_to_page.TransformRect(CFX_FloatRect(0, 0, 1.0f, 1.0f));
  bool touches = false;
  for (const auto& r : page_rects) {
    if (img_bbox_page.right > r.left && img_bbox_page.left < r.right &&
        img_bbox_page.top > r.bottom && img_bbox_page.bottom < r.top) {
      touches = true;
      break;
    }
  }
  if (!touches) {
    return {};
  }

  CPDF_Image* image = iobj->GetImage();
  CPDF_Document* doc = page->GetDocument();
  if (!image || !doc) {
    return {.succeeded = false};
  }
  const int W = image->GetPixelWidth();
  const int H = image->GetPixelHeight();
  if (W <= 0 || H <= 0) {
    return {.succeeded = false};
  }

  // Try to load the image via standard DIB path
  RetainPtr<CFX_DIBBase> dib = image->LoadDIBBase();
  if (!dib) {
    return {.succeeded = false};
  }

  const int bpp = dib->GetBPP();
  const bool is_mask = dib->IsMaskFormat();
  const bool has_alpha = dib->IsAlphaFormat();

  const bool is_1bit = (bpp == 1);
  const bool is_gray8 = (bpp == 8) && !is_mask;
  const bool is_rgb24 = (bpp == 24);
  const bool is_bgra32 = (bpp == 32) && has_alpha;
  const bool is_bgrx32 = (bpp == 32) && !has_alpha;

  // Check if this is an ImageMask - these use the fill color from graphics
  // state
  const bool is_image_mask = image->IsMask();
  uint8_t mask_fill_r = 0, mask_fill_g = 0, mask_fill_b = 0;
  if (is_image_mask) {
    // Get fill color from the image object's color state
    // FX_COLORREF is BGR: 0x00BBGGRR
    FX_COLORREF fill_color = iobj->color_state().GetFillColorRef();
    mask_fill_r = static_cast<uint8_t>(fill_color & 0xFF);
    mask_fill_g = static_cast<uint8_t>((fill_color >> 8) & 0xFF);
    mask_fill_b = static_cast<uint8_t>((fill_color >> 16) & 0xFF);
  }

  // Palette detection for indexed-8 images.
  auto palette = dib->GetPaletteSpan();
  const bool is_indexed8 = is_gray8 && !palette.empty();

  bool palette_has_alpha = false;
  if (is_indexed8) {
    for (uint32_t c : palette) {
      if ((c >> 24) != 0xFF) {
        palette_has_alpha = true;
        break;
      }
    }
  }

  if (!(is_1bit || is_gray8 || is_rgb24 || is_bgra32 || is_bgrx32)) {
    return {.succeeded = false};
  }

  RetainPtr<const CPDF_Dictionary> source_dict =
      image->GetStream() ? image->GetStream()->GetDict() : nullptr;

  // If the image has an SMask, keep it so we preserve transparency.
  RetainPtr<const CPDF_Stream> orig_smask_stream;
  if (source_dict) {
    RetainPtr<const CPDF_Object> smask_obj =
        source_dict->GetDirectObjectFor("SMask");
    if (smask_obj && smask_obj->AsStream()) {
      orig_smask_stream = pdfium::WrapRetain(smask_obj->AsStream());
    }
  }

  // Map page-space rects into image pixel space (bottom-up).
  std::vector<CFX_FloatRect> img_rects;
  PageRectsToImageGrid(img_to_page, W, H, page_rects, &img_rects);
  if (img_rects.empty()) {
    return {};
  }
  const std::vector<GridBox> boxes = GridBoxesOf(img_rects, W, H);
  if (boxes.empty()) {
    return {};
  }

  // EmbedPDF: an explicit mask stream masks the replacement too. A soft mask
  // overrides it (ISO 32000-1 §8.9.5, the SMask entry), and a colour-key mask
  // (an array) is already in the alpha CPDF_DIB reads.
  RetainPtr<CPDF_Stream> explicit_mask;
  RetainPtr<const CPDF_Stream> source_mask =
      source_dict && !orig_smask_stream ? source_dict->GetStreamFor("Mask")
                                        : nullptr;
  if (source_mask) {
    explicit_mask = RedactExplicitMask(doc, std::move(source_mask), img_to_page,
                                       page_rects);
    if (!explicit_mask) {
      return {.succeeded = false};
    }
  }

  const uint8_t fill_val = fill_black ? 0x00 : 0xFF;

  // Build new decoded buffers.
  DataVector<uint8_t> out_rgb(static_cast<size_t>(W) * static_cast<size_t>(H) *
                              3u);
  DataVector<uint8_t> out_a;

  // We need an alpha plane if: original was BGRA32, or there was an SMask, or
  // palette carries alpha (PNG paletted transparency), or it's an ImageMask.
  bool process_alpha = is_bgra32 || !!orig_smask_stream ||
                       (is_indexed8 && palette_has_alpha) || is_image_mask;

  if (process_alpha) {
    out_a.resize(static_cast<size_t>(W) * static_cast<size_t>(H));
    if (orig_smask_stream && !is_bgra32) {
      // Try to decode SMask as JPEG (for file-based streams in WASM)
      if (!DecodeJpegSMask(orig_smask_stream, W, H, out_a)) {
        // Fall back to LoadAllDataFiltered
        auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(orig_smask_stream);
        acc->LoadAllDataFiltered();
        pdfium::span<const uint8_t> span = acc->GetSpan();
        if (span.size() >= out_a.size()) {
          memcpy(out_a.data(), span.data(), out_a.size());
        } else if (!span.empty()) {
          memcpy(out_a.data(), span.data(), span.size());
          std::fill(out_a.begin() + static_cast<ptrdiff_t>(span.size()),
                    out_a.end(), 0xFF);
        } else {
          std::fill(out_a.begin(), out_a.end(), 0xFF);
        }
      }
    } else {
      std::fill(out_a.begin(), out_a.end(), 0xFF);
    }
  }

  size_t total_redacted_px = 0;

  for (int row_top = 0; row_top < H; ++row_top) {
    const int y_img = H - 1 - row_top;
    const pdfium::span<const uint8_t> sline = dib->GetScanline(row_top);
    uint8_t* drow_rgb = out_rgb.data() + static_cast<size_t>(row_top) *
                                             static_cast<size_t>(W) * 3u;
    uint8_t* arow = process_alpha
                        ? (out_a.data() + static_cast<size_t>(row_top) *
                                              static_cast<size_t>(W))
                        : nullptr;

    if (sline.empty()) {
      std::fill(drow_rgb, drow_rgb + static_cast<size_t>(W) * 3u, fill_val);
      if (process_alpha) {
        std::fill(arow, arow + static_cast<size_t>(W), 0xFF);
      }
      total_redacted_px += static_cast<size_t>(W);
      continue;
    }

    for (int x = 0; x < W; ++x) {
      const bool red = InAnyBox(x, y_img, boxes);

      if (red) {
        drow_rgb[3 * x + 0] = fill_val;
        drow_rgb[3 * x + 1] = fill_val;
        drow_rgb[3 * x + 2] = fill_val;
        if (process_alpha) {
          arow[x] = 0xFF;
        }
        ++total_redacted_px;
        continue;
      }

      if (is_1bit) {
        // 1-bit image: each byte contains 8 pixels, MSB first
        const int byte_idx = x / 8;
        const int bit_idx = 7 - (x % 8);  // MSB first
        const uint8_t byte_val = sline[byte_idx];
        const uint8_t bit_val = (byte_val >> bit_idx) & 1;

        uint8_t r, g, b;
        if (is_image_mask) {
          // ImageMask: use fill color for painted pixels, transparent for
          // others PDFium's DIB loader applies the Decode array during
          // decoding, so the decoded bit values are already transformed. After
          // decoding: bit 1 = painted, bit 0 = transparent
          const bool is_paint = (bit_val == 1);
          if (is_paint) {
            r = mask_fill_r;
            g = mask_fill_g;
            b = mask_fill_b;
            if (process_alpha) {
              arow[x] = 0xFF;  // opaque
            }
          } else {
            r = g = b = 0xFF;  // background (will be transparent)
            if (process_alpha) {
              arow[x] = 0x00;  // transparent
            }
          }
        } else if (!palette.empty()) {
          // Use palette - it already reflects correct color mapping (BlackIs1,
          // Decode, etc.)
          const uint32_t argb = palette[bit_val];
          r = static_cast<uint8_t>((argb >> 16) & 0xFF);
          g = static_cast<uint8_t>((argb >> 8) & 0xFF);
          b = static_cast<uint8_t>(argb & 0xFF);
        } else {
          // No palette: default is bit 0 = black, bit 1 = white
          const uint8_t v = bit_val ? 0xFF : 0x00;
          r = g = b = v;
        }
        drow_rgb[3 * x + 0] = r;
        drow_rgb[3 * x + 1] = g;
        drow_rgb[3 * x + 2] = b;
      } else if (is_indexed8) {
        const uint8_t idx = sline[x];
        const uint32_t argb = palette[idx];
        drow_rgb[3 * x + 0] = static_cast<uint8_t>((argb >> 16) & 0xFF);
        drow_rgb[3 * x + 1] = static_cast<uint8_t>((argb >> 8) & 0xFF);
        drow_rgb[3 * x + 2] = static_cast<uint8_t>(argb & 0xFF);
        if (process_alpha && !orig_smask_stream && !is_bgra32 &&
            palette_has_alpha) {
          arow[x] = static_cast<uint8_t>((argb >> 24) & 0xFF);
        }
      } else if (is_gray8) {
        const uint8_t v = sline[x];
        drow_rgb[3 * x + 0] = v;
        drow_rgb[3 * x + 1] = v;
        drow_rgb[3 * x + 2] = v;
      } else if (is_rgb24) {
        drow_rgb[3 * x + 0] = sline[3 * x + 2];
        drow_rgb[3 * x + 1] = sline[3 * x + 1];
        drow_rgb[3 * x + 2] = sline[3 * x + 0];
      } else {
        drow_rgb[3 * x + 0] = sline[4 * x + 2];
        drow_rgb[3 * x + 1] = sline[4 * x + 1];
        drow_rgb[3 * x + 2] = sline[4 * x + 0];
        if (process_alpha && is_bgra32) {
          arow[x] = sline[4 * x + 3];
        }
      }
    }
  }

  if (total_redacted_px == 0) {
    return {.succeeded = false};
  }

  // Ensure redaction regions are fully opaque in the SMask/alpha plane.
  if (process_alpha) {
    for (const auto& box : boxes) {
      for (int y = box.y0; y < box.y1; ++y) {
        const int row_top = H - 1 - y;
        uint8_t* row_ptr = out_a.data() + static_cast<size_t>(row_top) *
                                              static_cast<size_t>(W);
        std::fill(row_ptr + box.x0, row_ptr + box.x1, 0xFF);
      }
    }
  }

  // Build main image dict (decoded RGB).
  RetainPtr<CPDF_Dictionary> ndict = doc->New<CPDF_Dictionary>();
  ndict->SetNewFor<CPDF_Name>("Type", "XObject");
  ndict->SetNewFor<CPDF_Name>("Subtype", "Image");
  ndict->SetNewFor<CPDF_Number>("Width", W);
  ndict->SetNewFor<CPDF_Number>("Height", H);
  ndict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceRGB");
  ndict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);

  // If we have/kept alpha, attach a soft mask.
  if (process_alpha) {
    RetainPtr<CPDF_Dictionary> smask_dict = doc->New<CPDF_Dictionary>();
    smask_dict->SetNewFor<CPDF_Name>("Type", "XObject");
    smask_dict->SetNewFor<CPDF_Name>("Subtype", "Image");
    smask_dict->SetNewFor<CPDF_Number>("Width", W);
    smask_dict->SetNewFor<CPDF_Number>("Height", H);
    smask_dict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceGray");
    smask_dict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);

    RetainPtr<CPDF_Stream> smask_stream = pdfium::MakeRetain<CPDF_Stream>(
        std::move(out_a), std::move(smask_dict));
    const uint32_t smask_objnum = doc->AddIndirectObject(smask_stream);
    ndict->SetFor("SMask",
                  pdfium::MakeRetain<CPDF_Reference>(doc, smask_objnum));
  } else if (explicit_mask) {
    ndict->SetNewFor<CPDF_Reference>("Mask", doc, explicit_mask->GetObjNum());
  }

  // EmbedPDF: the replacement keeps what says how, where and when the image
  // is drawn: its optional content (an image in a hidden layer stays
  // hidden), rendering intent, interpolation and structure. Nothing else of
  // the original comes along: its metadata, alternate versions and OPI link
  // are other copies of the pixels.
  if (source_dict) {
    for (const char* key : {"OC", "Intent", "Interpolate", "StructParent"}) {
      if (RetainPtr<const CPDF_Object> value = source_dict->GetObjectFor(key)) {
        ndict->SetFor(key, value->CloneForHolder(doc));
      }
    }
  }

  // Images (and their masks) may be reused by other placements or pages.
  // Replace only this placement after all decoded buffers are ready. Editing
  // the old indirect stream in place would also redact the unmarked copies.
  auto replacement_stream =
      pdfium::MakeRetain<CPDF_Stream>(std::move(out_rgb), std::move(ndict));
  auto replacement = pdfium::MakeRetain<CPDF_Image>(doc, replacement_stream);
  replacement->ConvertStreamToIndirectObject();
  iobj->SetImage(std::move(replacement));
  page->ClearRenderContext();
  iobj->SetDirty(true);
  return {.changed = true};
}

}  // namespace

RedactResult RedactImageObject(CPDF_Page* page,
                               CPDF_ImageObject* image_object,
                               const CPDF_Dictionary* resources,
                               pdfium::span<const CFX_FloatRect> page_rects,
                               const CFX_Matrix& parent_to_page,
                               bool fill_black) {
  if (!page || !image_object) {
    return {.succeeded = false};
  }
  // The image's own matrix first, then its placement.
  const CFX_Matrix image_to_page = image_object->matrix() * parent_to_page;
  const CFX_FloatRect bounds =
      image_to_page.TransformRect(CFX_FloatRect(0, 0, 1.0f, 1.0f));
  const bool touches = std::ranges::any_of(page_rects, [&](const auto& r) {
    return bounds.right > r.left && bounds.left < r.right &&
           bounds.top > r.bottom && bounds.bottom < r.top;
  });
  if (!touches) {
    return {};
  }
  // A black fill shows only under the black boxes drawn over every region;
  // paper is as good there.
  std::optional<RedactResult> kept = RedactKeepingFormat(
      page, image_object, resources, page_rects, image_to_page);
  if (kept.has_value()) {
    return kept.value();
  }
  return RedactDecodedImage(page, image_object, page_rects, parent_to_page,
                            fill_black);
}
