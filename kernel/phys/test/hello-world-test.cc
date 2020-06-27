// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>

#include "test-main.h"

const char Symbolize::kProgramName_[] = "hello-world-test";

int TestMain(void*, arch::EarlyTicks) {
  printf("Hello, world!\n");
  return 0;
}
