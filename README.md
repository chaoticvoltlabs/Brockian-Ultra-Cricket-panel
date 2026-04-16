# BUC Panel

Firmware targets for multiple BUC panel hardware variants.

## What this repo is for

This repository serves two audiences:

1. people who want to build and personalize the full BUC ecosystem
2. people who already have supported panel hardware and want a reproducible starting point

This repo is the panel-side firmware layer in the wider BUC setup.

## Hardware targets

- [`targets/waveshare-4.3-esp32s3`](targets/waveshare-4.3-esp32s3/README.md)
  - live-oriented 4.3 inch panel firmware with compact weather, clock, indoor and control pages
- [`targets/waveshare-7b-esp32s3-showcase`](targets/waveshare-7b-esp32s3-showcase/README.md)
  - 7 inch showcase/reference firmware with the current PSRAM-backed, VSYNC-stable UI baseline

## How this fits into BUC

1. Home Assistant provides canonical entities and automations
2. BUC server reshapes those into compact panel-friendly models and control endpoints
3. the panel firmware in this repo renders those models locally and sends control intents back upstream

This repository owns:

- device-side UI rendering
- touch interaction
- hardware-specific display and touch setup
- Wi-Fi connectivity
- panel-side integration patterns for BUC-facing APIs

## Current direction

- keep the 4.3 inch target as the compact live baseline
- keep the 7B target as a strong showcase/reference for larger panel work
- only extract shared code after the per-target behavior is stable and well understood

## Transitional note

The repository root still contains the original 4.3 inch layout for compatibility while the `targets/` structure is being refined. New work should prefer the target directories.

## Related repositories

- [Brockian-Ultra-Cricket-server](https://github.com/chaoticvoltlabs/Brockian-Ultra-Cricket-server)
  - BUC server and compact panel-facing API layer
- [Brockian-Ultra-Cricket-homeassistant](https://github.com/chaoticvoltlabs/Brockian-Ultra-Cricket-homeassistant)
  - Home Assistant package and payload layer

## Build

Build from inside the target directory you want to use.

```bash
cd targets/waveshare-4.3-esp32s3
idf.py build
```

```bash
cd targets/waveshare-7b-esp32s3-showcase
idf.py build
```

## Copyright & license

PolyForm Noncommercial License 1.0.0
with Commercial Use by Explicit Permission Only
See LICENSE.txt


Copyright (c) 2026 Robin Kluit / Chaoticvolt.
