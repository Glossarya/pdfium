// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_FXCODEC_JPEG_JPEG_WRITER_H_
#define CORE_FXCODEC_JPEG_JPEG_WRITER_H_

#include <stdint.h>

#include <functional>
#include <optional>

#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/span.h"

namespace fxcodec {

// A rectangle of a JPEG's pixels: columns [left, right), rows [top, bottom)
// from the top.
struct JpegRect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

// `jpeg` with every pixel in `rects` set to `fill`, edited in the JPEG's 8 x 8
// blocks of coefficients, the way jpegtran transforms an image: a block
// wholly in the rects becomes flat, a block a rect's edge crosses is decoded,
// filled and encoded again with the same quantization, every other block is
// copied as is. Chroma blocks of a subsampled JPEG cover several pixels; one
// a rect touches is filled whole.
//
// `fill` holds a value per component as the decoder hands components out:
// gray, RGB (or the stored values of a 3-component JPEG read without the
// colour transform) or CMYK. `color_transform` is the /DCTDecode filter's
// ColorTransform, read as JpegModule::CreateDecoder() reads it.
//
// The result keeps the JPEG's coding (baseline or progressive), its
// quantization and its colour transform marker (JFIF or Adobe), and no other
// marker: comments, EXIF and its thumbnail, XMP and ICC profiles go. Nullopt
// when the JPEG isn't `width` x `height` with `components` 8-bit components,
// or libjpeg reads it with an error or a warning.
std::optional<DataVector<uint8_t>> FillJpegRects(
    pdfium::span<const uint8_t> jpeg,
    int width,
    int height,
    int components,
    bool color_transform,
    pdfium::span<const JpegRect> rects,
    pdfium::span<const uint8_t> fill);

// A JPEG of `width` x `height` pixels with `components` 8-bit components
// (1: gray, 3: RGB) at `quality` (1 to 100), with Huffman tables made for it.
// `row(y, out)` writes row `y` from the top into `out`. Nullopt if libjpeg
// fails.
std::optional<DataVector<uint8_t>> EncodeJpeg(
    int width,
    int height,
    int components,
    int quality,
    const std::function<void(int y, pdfium::span<uint8_t> out)>& row);

}  // namespace fxcodec

#endif  // CORE_FXCODEC_JPEG_JPEG_WRITER_H_
