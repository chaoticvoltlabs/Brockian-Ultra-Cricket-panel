# Control Page

This file keeps the original name for continuity, but on the current 4.3 target the control page is page 4.

## Concept

The control page is the scene/direct-control page for the compact panel client.

- top row: scenes
- lower section: direct control

The semantics are split cleanly between scene activation and direct toggles.

## Scenes

Top row labels:

- `Work`
- `Evening`
- `Movie`
- `Night`

Scene commands sent to BUC:

```json
{"target":"scene_work","action":"activate"}
{"target":"scene_evening","action":"activate"}
{"target":"scene_movie","action":"activate"}
{"target":"scene_night","action":"activate"}
```

Long press:

- long press on the currently active scene triggers `scene_night`

## Direct controls

Current lower labels:

- `Light A`
- `Light B`
- `Light C`
- `Media`

Current direct control payloads:

```json
{"target":"light_a","action":"toggle"}
{"target":"light_b","action":"toggle"}
{"target":"light_c","action":"toggle"}
{"target":"media_power","action":"toggle"}
```

## Active-scene indication

The active scene is currently shown locally on the panel using:

- a subtle accent line
- brighter label text

This is a local UI state, not yet synchronized back from Home Assistant.

## Back-end assumptions

BUC must support:

- `POST /api/panel/control`

Home Assistant must expose example-compatible targets such as:

- `scene.room_work`
- `scene.room_evening`
- `scene.room_movie`
- `scene.room_night`
- `light.room_light_a`
- `switch.room_light_b`
- `light.room_light_c`
- `switch.media_power`
