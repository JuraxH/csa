// Copyright 2016 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef UTIL_STRUTIL_H_
#define UTIL_STRUTIL_H_

#include <string>

#include "stringpiece.h"
#include "util.h"

namespace re2 {
std::string StringPrintf(const char* format, ...);

}  // namespace re2

#endif  // UTIL_STRUTIL_H_
