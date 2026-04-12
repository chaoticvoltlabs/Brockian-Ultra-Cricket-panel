# Build And Release

## Prerequisites

- ESP-IDF v6 environment
- working `idf.py`
- local `main/secrets.h` copied from `main/secrets.example.h`

Example environment activation:

```bash
source /home/icz8922/.espressif/v6.0/esp-idf/export.sh
```

## Build

```bash
idf.py build
```

Artifacts:

- `build/buc_panel_client.bin`
- `build/buc_panel_client.elf`

## Flash and monitor

```bash
idf.py -p PORT flash
```

```bash
idf.py -p PORT monitor
```

## Release flow

The current working release pattern is:

1. implement and test locally
2. build with `idf.py build`
3. commit the intended panel-only changes
4. create a semver tag
5. push `main` and the tag

Example:

```bash
git push origin main
git push origin v0.7.0
```

## Current known-good milestone

- `v0.7.0`

Includes:

- page-3 scenes
- page-3 direct controls
- improved swipe handling across the full panel
