# Panel Architecture

## Purpose

This firmware renders the user-facing panel UI locally on the ESP32-S3 using LVGL. It is not a thin image viewer; it is an interactive client that talks to BUC over HTTP.

## System position

The current chain is:

1. Home Assistant normalizes data and owns scenes/switches.
2. BUC exposes compact panel APIs and translates control intents to HA service calls.
3. This firmware polls BUC and renders the resulting state locally.

## Main modules

- [`main.c`](../main/main.c)
  - LVGL startup
  - page construction
  - page-3 command wiring
- [`hw_init.c`](../main/hw_init.c)
  - panel, RGB bus, backlight init
- [`hw_touch.c`](../main/hw_touch.c)
  - GT911 touch integration
- [`panel_api.c`](../main/panel_api.c)
  - read path and control path HTTP calls
- [`ui_pages.c`](../main/ui_pages.c)
  - horizontal navigation
- [`ui_weather.c`](../main/ui_weather.c)
  - shared left column
- [`ui_indoor.c`](../main/ui_indoor.c)
  - page 2 grid
- [`ui_controls.c`](../main/ui_controls.c)
  - page 3 scenes and direct controls

## Current UI model

### Left column

Shared across pages:

- main weather number
- secondary weather/room line
- wind and gust
- pressure trend

### Right pages

- page 1: weather instruments
- page 2: indoor climate grid
- page 3: scenes and direct controls

## Current network contracts

### Read path

- `GET /api/panel/weather`

The panel polls this endpoint and updates:

- weather column
- pressure trend
- indoor tile data

### Control path

- `POST /api/panel/control`

Compact request shape:

```json
{
  "target": "light_a",
  "action": "toggle"
}
```

## Interaction notes

- swipe navigation is handled at screen level for predictable one-page movement
- button gestures bubble upward so swipe and tap can coexist
- page 3 scene highlighting is currently local and optimistic, not HA-readback driven
