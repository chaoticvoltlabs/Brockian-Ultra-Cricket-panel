# Build And Release

## Prerequisites

- ESP-IDF v6 environment
- working `idf.py`
- local secrets/config header copied from the target example file

Example environment activation:

```bash
source /home/icz8922/.espressif/v6.0/esp-idf/export.sh
```

## Build

Build from inside the target you want to ship.

```bash
cd targets/waveshare-4.3-esp32s3
idf.py build
```

```bash
cd targets/waveshare-7b-esp32s3-showcase
idf.py build
```

## Flash and monitor

```bash
idf.py -p PORT flash
```

```bash
idf.py -p PORT monitor
```

## Release checklist

1. build the affected target locally
2. sanity-check target docs and root docs
3. verify public example identifiers are generic enough
4. commit only the intended release changes
5. create a semver tag
6. push `main` and the tag

Example:

```bash
git push origin main
git push origin v0.9.0
```

## Current release baseline

The current public baseline should include:

- 4.3 target with restored swipe, day/night theming, global storm indicator and current clock/compass visual language
- 7B showcase target with the PSRAM-backed LVGL heap path and the VSYNC-stable display baseline

## Notes

- do not publish private room names, MAC addresses, secrets, or installation-specific command identifiers without first genericizing them
- for the 7B target, keep the LVGL heap override and `CONFIG_LV_CONF_SKIP=y` notes aligned with the target README
