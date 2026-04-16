# Panel Architecture

## Purpose

This firmware renders the panel UI locally on the ESP32-S3 with LVGL. It is an interactive client that talks to BUC over HTTP for both readback and control.

## System position

The runtime chain is:

1. Home Assistant owns the canonical entities, scenes and helper state.
2. BUC reshapes those into compact panel payloads and control endpoints.
3. The panel polls BUC, renders locally, and posts user actions back upstream.

## Main modules

- [`main.c`](../main/main.c)
  - LVGL startup
  - display/touch bring-up
  - page construction
- [`panel_api.c`](../main/panel_api.c)
  - `GET /api/panel/config`
  - `GET /api/panel/weather`
  - `POST /api/panel/control`
- [`ui_pages.c`](../main/ui_pages.c)
  - horizontal swipe container for the right-side page stack
- [`ui_weather.c`](../main/ui_weather.c)
  - shared left weather column
  - global storm warning indicator
- [`ui_compass.c`](../main/ui_compass.c)
  - compass / wind rose page
- [`ui_clock.c`](../main/ui_clock.c)
  - analog clock page
- [`ui_indoor.c`](../main/ui_indoor.c)
  - indoor climate page
- [`ui_controls.c`](../main/ui_controls.c)
  - scenes and direct-control page
- [`ui_theme.c`](../main/ui_theme.c)
  - renderer-wide day/night palette switching

## Current 4.3 page model

### Left column

Shared across all pages:

- outdoor temperature / feels like
- wind and gust
- pressure trend
- global storm warning overlay

### Right pages

- page 1: compass and wind strip
- page 2: analog clock
- page 3: indoor climate
- page 4: scenes and direct control

## Current network contracts

### Read path

- `GET /api/panel/config`
- `GET /api/panel/weather`

The panel consumes:

- current outdoor weather
- `pressure_trend_24h`
- indoor zones
- optional `night_mode`
- optional `page3_target_states`
- optional ambient fields such as `ambient_brightness_pct` and `ambient_rgb`

### Control path

- `POST /api/panel/control`

Compact request shape:

```json
{
  "target": "light_a",
  "action": "toggle"
}
```

Ambient-style controls may also include payload:

```json
{
  "target": "ambient",
  "action": "set_rgb",
  "rgb": [255, 136, 32]
}
```

## Interaction and rendering notes

- the 4.3 target currently relies on the custom swipe container in `ui_pages.c`; replacing it blindly with another navigation mechanism risks reintroducing swipe regressions
- the 4.3 renderer uses the VSYNC-driven full-refresh path to avoid visible tearing
- day/night theming is driven from upstream `night_mode`, not from a local schedule
- the storm warning indicator is global so warnings stay visible regardless of the current page
