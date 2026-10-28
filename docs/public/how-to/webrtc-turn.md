---
title: WebRTC and TURN
sidebar_position: 3
sidebar_label: WebRTC and TURN
description: Use a TURN relay when the browser cannot connect directly to Open Perception Kit media.
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->


# WebRTC and TURN

TURN relays WebRTC media when NAT, a firewall, or container networking prevents
a direct connection between the browser and OPK. Loading the web page or
connecting to signaling does not confirm that video can arrive.

The WSL and macOS development workflows enable the coturn container
automatically. Linux and Raspberry Pi normally use host networking and do not
need a relay when the browser can reach OPK directly.

## Start A Relay

If your workflow already started coturn, reuse it. Otherwise, run this from the
repository root in the **Docker host shell**. Replace `192.168.1.10` with the
host address reachable from both the browser and OPK; do not use `localhost`.

```bash
WEBRTC_HOST_IP=192.168.1.10 docker compose \
  -f .devcontainer/compose.devcont.yaml \
  -f .devcontainer/docker-compose.devcont.turn.yaml up -d coturn
```

The bundled relay publishes TCP/UDP port `3478` and UDP ports `49000-49050`.
Allow these ports through the host firewall.

## Configure The Pipeline

In the shell that launches the pipeline, usually the **Docker shell**, set the
relay address before starting OPK:

```bash
export OPK_WEBRTC_TURN_SERVER='turn://test:test123@192.168.1.10:3478'
```

Use the same host address as above. `test` and `test123` are the bundled demo
credentials; for an existing TURN service, use its address and credentials.
Restart your pipeline from that shell and reload the browser. OPK supplies the
relay configuration to the browser automatically.

If video still does not arrive, also confirm that the pipeline is running and
its input is producing frames. Missing video alone does not identify a TURN
failure.
