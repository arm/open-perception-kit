---
title: WebRTC and TURN
sidebar_position: 3
sidebar_label: WebRTC and TURN
description: When TURN is needed, how to enable it, and how to troubleshoot missing video.
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

TURN relays video when NAT, container networking, or a firewall blocks a direct
WebRTC connection between OPK and the browser. The web page can load even when
the video connection fails.

## Development containers

WSL and macOS development workflows start coturn and configure OPK automatically.
Linux and Raspberry Pi normally use host networking and work without a relay
when the browser can reach OPK directly.

Reopen the VS Code devcontainer if its host IP changes or coturn stops. A changed
IP recreates both containers. Project files in `/work` and Docker volumes are kept;
changes stored only inside the containers are lost. These containers do not
start automatically with Docker.

## Enable TURN manually

If your setup needs a relay and does not start one, run this from the repository
root in the **host shell**. Replace `192.168.1.10` with a host IP reachable from
both OPK and the browser. Do not use `localhost`.

```bash
WEBRTC_HOST_IP=192.168.1.10 docker compose \
  -f .devcontainer/compose.devcont.yaml \
  -f .devcontainer/docker-compose.devcont.turn.yaml up -d coturn
```

Allow TCP/UDP port `3478` and UDP ports `49000-49050` through the host firewall.

In the **shell that launches the pipeline**, set the relay URL before starting
OPK:

```bash
export OPK_WEBRTC_TURN_SERVER='turn://test:test123@192.168.1.10:3478'
```

Use the same IP in both commands. `test` and `test123` are the bundled coturn
credentials. To use an existing TURN service, skip the coturn command and set
its URL and credentials instead. Restart the pipeline and reload the browser;
OPK sends the TURN configuration to the browser.

## Missing or black video

`Connected` means the browser has received video frames. If the picture is black,
check the source video and pipeline output.

| Browser message | What to check |
| --- | --- |
| `Connection failed. Try TURN.` | No TURN server is configured. Enable a relay using the steps above. |
| `Connection failed. Check network/TURN.` | Check that the relay is running and its IP, credentials, and firewall ports are correct. |
| `Waiting for video…` or `No video. Retrying…` | Check that the pipeline is running and its input produces frames. Check the browser and pipeline logs if video still does not arrive. |
| `Signaling unavailable. Check server.` | Check that OPK is running and the signaling server is reachable. |
| `Playback failed. Check browser.` | Check browser playback permissions and the browser console. |

The browser retries dropped connections automatically. Missing video alone does
not mean TURN is required.
