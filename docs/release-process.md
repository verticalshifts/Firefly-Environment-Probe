# Release process

This is the operational runbook for cutting a release that the firmware's
opt-in auto-update checker (`src/ota/OTAUpdateChecker.h`, see
[configuration.md](configuration.md)'s "OTA update checking" section) can
actually find and install. Every step here is a hard requirement, not a
suggestion — get any of the naming or checksum-formatting wrong and the
release is silently invisible to devices checking for updates (or, worse,
visible but un-installable).

## 1. Bump the version

Update `FIRMWARE_VERSION` in `platformio.ini`'s `[env]` `build_flags`
(currently `-DFIRMWARE_VERSION=\"1.0.0\"`). This is what every device
compares its own version against — the tag you push must match it exactly.

## 2. Build both platform binaries

```bash
pio run -e esp32
pio run -e esp8266
```

## 3. Rename the binaries to the exact expected asset names

The device matches release assets by exact filename — anything else and
it won't find an installable asset for its platform:

```bash
cp .pio/build/esp32/firmware.bin   firmware-esp32.bin
cp .pio/build/esp8266/firmware.bin firmware-esp8266.bin
```

## 4. Compute the checksums

```bash
shasum -a 256 firmware-esp32.bin firmware-esp8266.bin
```

## 5. Create the release, with the checksums in the release notes

The device reads the checksum straight out of the release description
text (the same, already-fetched API response used to find the release —
no extra download, no extra trust chain) — it must contain these two
exact-format lines somewhere in the body:

```
SHA256_ESP32: <hex from step 4>
SHA256_ESP8266: <hex from step 4>
```

**A release with no checksum lines is un-installable by design** — the
install endpoint (`POST /api/ota/install-latest`) refuses to proceed
without one, on purpose (see `docs/configuration.md`'s "OTA update
checking" section for why). This is easy to forget — don't.

```bash
gh release create v1.1.0 \
  --repo verticalshifts/Firefly-Environment-Probe \
  --title "v1.1.0" \
  --notes "$(printf 'What changed...\n\nSHA256_ESP32: %s\nSHA256_ESP8266: %s\n' \
    "$(shasum -a 256 firmware-esp32.bin | cut -d' ' -f1)" \
    "$(shasum -a 256 firmware-esp8266.bin | cut -d' ' -f1)")" \
  firmware-esp32.bin firmware-esp8266.bin
```

The tag (`v1.1.0` here) must be `v` + the exact `FIRMWARE_VERSION` from
step 1 — the device strips a leading `v` and compares the rest
numerically (`major.minor.patch`), so `v1.9.0` vs `v1.10.0` compares
correctly, but anything that doesn't parse as three dot-separated integers
is silently treated as "can't compare" (never as "is newer") rather than
risking a bad update decision.

## 6. Verify

Devices with `otaCheckEnabled` will pick this up within their configured
`otaCheckIntervalS` (default 6h), or immediately via Settings → "Check
Now". Confirm via `GET /api/ota/status` on a test device before announcing
the release more broadly — see `docs/api.md`'s `/api/ota/status` for the
expected response shape, and the negative test worth running once per
release process change (not per release): publish a deliberately wrong
checksum on a throwaway test release and confirm the device refuses to
install it and leaves the running firmware untouched.
