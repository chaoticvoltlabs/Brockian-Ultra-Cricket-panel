# Waveshare 4.3 ESP32-S3

Live-oriented BUC panel firmware for the Waveshare ESP32-S3-LCD-4.3.

## Pages

- page 1: compass + wind strip
- page 2: analog clock
- page 3: indoor climate
- page 4: scenes and direct control

## Role

This is the compact live panel target that talks directly to the BUC server:

- polls `GET /api/panel/config`
- polls `GET /api/panel/weather`
- posts `POST /api/panel/control`

## Current technical baseline

- swipe navigation is restored on the 4-page layout
- renderer-wide night mode is driven from the upstream `night_mode` field
- the storm warning triangle is a global overlay, visible on every page
- the compass and wind strip follow the current clock-oriented visual language

## Local config

Create `main/secrets.h` from `main/secrets.example.h`.

## Build

```bash
cd targets/waveshare-4.3-esp32s3
idf.py build
```
