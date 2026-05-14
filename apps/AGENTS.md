# Apps AGENTS Guide

This file is the app-layer starting point for coding agents working under `apps/`.

Use it when the task is:
- adding a new browser or local app under `apps/`
- modifying an app that consumes AMP metadata
- documenting app-specific metadata assumptions

## Start here

Read these first:
- `AGENTS.md` at the repository root
- `metadata/AGENTS.md`
- `metadata/api/README.md`

If the app consumes metadata in a browser, also inspect:
- `apps/metadata_browser/`

Treat `apps/metadata_browser` as the checked-in browser starter template for:
- WebSocket connect/disconnect
- transport-message normalization
- safe traversal of `perception.layers`
- rendering simple derived summaries without app-specific assumptions

## Expected browser metadata app behavior

Browser metadata apps should:
- connect to `pekcomm` over WebSocket
- parse one JSON transport message per text frame
- handle the current transport envelope:
  - `{ "frame_counter": <number>, "perception": { ... } }`
- handle `message.perception == null` gracefully
- route on `layer.contentType` and `detection.type`
- prefer `VideoFrame` dimensions when coordinate mapping matters
- keep app-specific control logic, mirroring, smoothing, and dropout behavior inside the app

## Documentation rule

Every app that consumes metadata should have a local `README.md` that explicitly documents:
- transport endpoint expectations
- payload shape assumptions
- coordinate-space policy
- which `contentType` and detection `type` values it uses
- any smoothing, fallback, or calibration logic

Do not let app-specific assumptions become implicit contract.

## When extending existing apps

Prefer modifying the closest checked-in app surface instead of inventing a fresh structure.

For browser metadata tools:
- start from `apps/metadata_browser`
- keep generic transport helpers generic
- only add app-specific behavior once the metadata contract is explicit

## Verification

For app-only changes, verify at minimum:
- the README matches the code behavior
- the app handles empty or missing metadata without crashing
- the app tolerates unknown layers/detections
- the default WebSocket endpoint matches the documented expectation
