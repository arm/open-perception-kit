# Containerized Execution Model
## WebRTC-Based Media Integration

<img src="webrtc.jpg" alt="Containerized pipeline architecture" width="600">

The system is designed around a container-first development and deployment model.

Developers pull the repository and build the development container.
This provides a consistent and reproducible working environment across platforms.

The container executes the GStreamer pipeline, including inference workloads.

---

## Development Model

- The repository defines the full build environment.
- The container encapsulates all dependencies.
- The same environment is used across Linux, macOS (via virtualization), and edge devices.
- This eliminates host-specific configuration drift.

---

## Challenges of Container Isolation

While containers provide reproducibility, they introduce runtime challenges:

- The container is isolated from the host display server.
- Audio playback inside the container is non-trivial.
- Direct GPU / display integration may be constrained.
- UDP-based audio/video streaming is often laggy and unstable.
- Media debugging becomes difficult with traditional forwarding approaches.

---

## AmpSink: WebRTC-Based Output

To solve these issues, the system introduces **AmpSink**.

AmpSink:

- Streams media using WebRTC.
- Provides low-latency video and audio output.
- Avoids unstable UDP streaming.
- Works reliably across host platforms.
- Enables browser-based visualization of pipeline output.

The host web browser connects to the container and renders the media stream.

This provides:

- Stable playback
- No perceptible lag
- Interactive debugging
- Platform-independent viewing

---

## REST Control Interface

The browser communicates with the container using REST control commands.

This allows:

- Pipeline start/stop control
- Runtime parameter updates
- Triggering actions
- Service management

Media transport and control signaling are cleanly separated.

---

## AmpSource (Planned)

AmpSource extends the architecture in the opposite direction.

Concept:

- A device boots and starts the pipeline automatically.
- The pipeline runs inside the container.
- The user opens a browser and connects via WebRTC.
- The browser provides webcam/microphone input.
- Media is streamed into the pipeline.

This enables:

- Headless device deployment
- Remote operation
- Zero-install user interaction
- Embedded edge scenarios

---

## Architectural Benefits

- Containerized reproducibility
- WebRTC-based stable media streaming
- No host display dependencies
- Clean separation of runtime and presentation layers
- Browser-based universal interface
- Edge-ready deployment model

This model allows development, testing, and deployment using the same architecture,
from local machines to Raspberry Pi 5 and other edge devices.