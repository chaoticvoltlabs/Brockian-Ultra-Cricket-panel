# BUC Panel

Firmware targets for multiple BUC panel hardware variants.

## What this repo is for

This repository serves two audiences:

1. people who want to build and personalize the full BUC ecosystem
2. people who already have supported panel hardware and want to get a panel running with as little guesswork as possible

This repo is the panel-side firmware layer in the wider BUC setup.

## Choose your path

### I have supported hardware and want to get a panel running

Start with the hardware target that matches your device.

### I want to build and personalize the full BUC ecosystem

Start here at the repo root, read the hardware overview below, and then combine this panel firmware with the related BUC server and Home Assistant repositories.

## Hardware targets

- [`targets/waveshare-4.3-esp32s3`](targets/waveshare-4.3-esp32s3/README.md)
  - the current live-oriented 4.3 inch panel client
- [`targets/waveshare-7b-esp32s3-showcase`](targets/waveshare-7b-esp32s3-showcase/README.md)
  - a 7 inch showcase/reference build with clock, storm forecast, and mancave control pages

## Why the repo is structured this way

Different panel builds are not just cosmetic variants. Display timing, touch wiring, resolution, and UI density all change with the hardware. The repository now keeps those distinctions explicit so builders can start from the right target instead of reverse-engineering which files belong to which panel.

## How this fits into BUC

The broader chain is:

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

- keep the 4.3 inch target focused on the live BUC data path
- keep the 7B control page as a showcase/reference instead of immediately folding it into live integration
- move shared code out later, only after each hardware target is clear and stable

## Transitional note

The repository root still contains the original 4.3 inch layout for compatibility while the new `targets/` structure is being established. New work should prefer the target directories above.

## Related repositories

- [Brockian-Ultra-Cricket-server](https://github.com/chaoticvoltlabs/Brockian-Ultra-Cricket-server)
  - BUC server and compact panel-facing API layer
- [Brockian-Ultra-Cricket-homeassistant](https://github.com/chaoticvoltlabs/Brockian-Ultra-Cricket-homeassistant)
  - Home Assistant package and payload layer

## Quick guidance

- If you want working panel firmware quickly: pick a folder under `targets/` and stay inside that target.
- If you want to personalize the whole stack: decide which target is your hardware baseline first, then adapt the server, HA entities, and panel UI together.

## Build

Build from inside the target directory you want to use. Each target has its own `README.md`, `main/`, partition layout, and local config expectations.

## Copyright & license

PolyForm Noncommercial License 1.0.0
with Commercial Use by Explicit Permission Only
See LICENSE.txt


Copyright (c) 2026 Robin Kluit / Chaoticvolt.
