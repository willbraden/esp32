#pragma once
// Mozilla root CA bundle (certs/x509_crt_bundle.bin, 146 roots), embedded by
// board_build.embed_files in platformio.ini. Pass to
// NetworkClientSecure::setCACertBundle() for real certificate validation.
#include <stdint.h>
#include <stddef.h>

extern const uint8_t ca_bundle_start[] asm("_binary_certs_x509_crt_bundle_bin_start");
extern const uint8_t ca_bundle_end[] asm("_binary_certs_x509_crt_bundle_bin_end");

static inline size_t ca_bundle_size() { return (size_t)(ca_bundle_end - ca_bundle_start); }
