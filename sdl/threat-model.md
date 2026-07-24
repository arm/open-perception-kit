# SDL Threat Model Notes

This file tracks security and privacy considerations that are valid threat model
items but are not necessarily immediate implementation blockers.

## LAN-Accessible Metadata WebSocket

**Status:** Accepted for now; revisit before exposing PEK outside trusted LANs.

**Context:** The WebUI metadata panels consume live `pekcomm` metadata over a
WebSocket on port `8002`. The devcontainer publishes this port so browsers on
the host or LAN can subscribe to the stream.

**Threat:** Any client that can reach the host and connect to `/ws` can read live
perception metadata. Browser pages from other origins may also attempt a
cross-site WebSocket connection to the LAN or localhost endpoint unless the
server validates `Origin` or requires authentication.

**Impact:** The metadata stream can include live detections, model output,
classification labels, OCR text, and runtime performance data. This may reveal
scene content or system behavior to unintended clients on the same network.

**Current rationale:** LAN accessibility is an intentional feature for current
development and demo workflows. The project does not currently define a stable
set of allowed clients or origins in advance, so enforcing an allowlist now would
risk breaking expected LAN usage.

**Possible future mitigations:**

- Add configurable `Origin` validation for browser WebSocket clients.
- Add token-based access control for `pekcomm` metadata subscriptions.
- Add a deployment mode that binds metadata ports to loopback for local-only
  development.
- Document that published metadata ports should only be used on trusted LANs.
