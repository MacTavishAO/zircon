// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_CRYPTO_TEST_UTILS_H_
#define ZIRCON_SYSTEM_ULIB_CRYPTO_TEST_UTILS_H_

#include <zircon/status.h>
#include <zircon/types.h>

#include <crypto/aead.h>
#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <crypto/hkdf.h>
#include <crypto/secret.h>

namespace crypto {
namespace testing {

// Resizes |out| and sets its contents to match the given |hex| string.
zx_status_t HexToBytes(const char* hex, Bytes* out);
zx_status_t HexToSecret(const char* hex, Secret* out);

// Fills the given |key| and |iv| with as much random data as indicated by |Cipher::GetKeyLen| and
// |Cipher::GetIVLen| for the given |cipher|. |iv| may be null.
zx_status_t GenerateKeyMaterial(Cipher::Algorithm cipher, Secret* key, Bytes* iv);

// Fills the given |key|, |iv| with as much random data as indicated by |AEAD::GetKeyLen| and
//|AEAD::GetIVLen| for the given |aead|. |iv| may be null.
zx_status_t GenerateKeyMaterial(AEAD::Algorithm aead, Secret* key, Bytes* iv);

// Returns true if and only if |len| bytes starting from |off| in |buf| match |val|.
bool AllEqual(const Bytes& buf, uint8_t val, zx_off_t off, size_t len);

}  // namespace testing
}  // namespace crypto

#endif  // ZIRCON_SYSTEM_ULIB_CRYPTO_TEST_UTILS_H_
