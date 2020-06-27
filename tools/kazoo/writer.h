// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_KAZOO_WRITER_H_
#define ZIRCON_TOOLS_KAZOO_WRITER_H_

#include <stdio.h>
#include <zircon/compiler.h>

#include <string>

#include "tools/kazoo/macros.h"

class Writer {
 public:
  Writer();

  // Unformatted string output.
  void Puts(const std::string& str);

  // Formatted string output.
  __PRINTFLIKE(2, 3) void Printf(const char* format, ...);

  // Prints a newline character if (and only if) the last line was not empty.
  void PrintSpacerLine();

  const std::string& Out() const { return out_; }

 private:
  std::string out_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Writer);
};

// Write |data| to |filename|, but avoid modifying the file's timestamp if
// it already contains |data|, in order to avoid causing unnecessary
// rebuilds of dependencies.
bool WriteFileIfChanged(const std::string& filename, const std::string& data);

#endif  // ZIRCON_TOOLS_KAZOO_WRITER_H_
