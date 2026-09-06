#pragma once
// -----------------------------------------------------------------------------
// GitHubApiRootCA.h
//
// Sectigo Public Server Authentication Root E46 — the long-term-stable
// trust anchor for api.github.com's certificate chain (leaf *.github.com,
// issued by intermediate "Sectigo Public Server Authentication CA DV E36",
// chaining to this self-signed root). ECDSA (not RSA) — chosen deliberately
// over pinning nothing: unlike Gen2Telemetry's setInsecure() (justified
// there because a spoofed sensor reading is low-stakes), a MITM'd OTA
// version-check response could feed the device a malicious download URL,
// so this connection is fully certificate-verified. The ECDSA leaf should
// verify meaningfully faster than the RSA-4096 chain that made Gen2's own
// pinned-CA attempt cause real WiFi packet loss — confirm with a live
// timing measurement on the physical board (see docs/architecture.md's
// "OTA rollback safety" / update-checking section).
//
// Verified two ways before embedding: sourced from Homebrew's OpenSSL CA
// bundle (Mozilla's trusted root store, not a random web fetch), and
// confirmed with `openssl verify -CAfile <this> <api.github.com's live
// chain>` — validates OK. Root (not leaf/intermediate, which rotate),
// valid until 2046.
//
// Used by src/ota/OTAUpdateChecker.cpp for the GET to
// api.github.com/repos/.../releases/latest. NOT the same host, and NOT the
// same root, as the actual firmware asset download — see
// GitHubAssetRootCA.h for that (different CA entirely: Let's Encrypt).
// -----------------------------------------------------------------------------

static const char GITHUB_API_ROOT_CA[] =
"-----BEGIN CERTIFICATE-----\n"
"MIICOjCCAcGgAwIBAgIQQvLM2htpN0RfFf51KBC49DAKBggqhkjOPQQDAzBfMQsw\n"
"CQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1T\n"
"ZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwHhcN\n"
"MjEwMzIyMDAwMDAwWhcNNDYwMzIxMjM1OTU5WjBfMQswCQYDVQQGEwJHQjEYMBYG\n"
"A1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1YmxpYyBT\n"
"ZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwdjAQBgcqhkjOPQIBBgUrgQQA\n"
"IgNiAAR2+pmpbiDt+dd34wc7qNs9Xzjoq1WmVk/WSOrsfy2qw7LFeeyZYX8QeccC\n"
"WvkEN/U0NSt3zn8gj1KjAIns1aeibVvjS5KToID1AZTc8GgHHs3u/iVStSBDHBv+\n"
"6xnOQ6OjQjBAMB0GA1UdDgQWBBTRItpMWfFLXyY4qp3W7usNw/upYTAOBgNVHQ8B\n"
"Af8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNnADBkAjAn7qRa\n"
"qCG76UeXlImldCBteU/IvZNeWBj7LRoAasm4PdCkT0RHlAFWovgzJQxC36oCMB3q\n"
"4S6ILuH5px0CMk7yn2xVdOOurvulGu7t0vzCAxHrRVxgED1cf5kDW21USAGKcw==\n"
"-----END CERTIFICATE-----\n";
