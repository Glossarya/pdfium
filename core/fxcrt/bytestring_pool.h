// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FXCRT_BYTESTRING_POOL_H_
#define CORE_FXCRT_BYTESTRING_POOL_H_

#include <unordered_set>

#include "core/fxcrt/bytestring.h"

namespace fxcrt {

class ByteStringPool {
 public:
  ByteStringPool();
  ~ByteStringPool();

  ByteString Intern(const ByteString& str);

  // EmbedPDF(save-traversal): the save reader drops its interned strings
  // between objects, so a full rewrite does not keep every name it read.
  void Clear() { pool_.clear(); }

 private:
  std::unordered_set<ByteString> pool_;
};

}  // namespace fxcrt

using fxcrt::ByteStringPool;

#endif  // CORE_FXCRT_BYTESTRING_POOL_H_
