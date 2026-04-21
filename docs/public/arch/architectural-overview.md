---
sidebar_position: 3
sidebar_label: Architectural overview
---

# Architectural Overview
## AMP execution model and GStreamer integration

AMP Development Forge is a GStreamer-centric inference execution framework, designed to run AI workloads in media pipelines.
GStreamer provides the media transport and scheduling, while the core execution model is independent and can run without GStreamer.
In this architecture, GStreamer primarily feeds audio/video buffers into the system and carries results downstream as metadata.

---

## System Composition

The system is implemented as a set of reusable GStreamer elements that can be inserted into existing pipelines.

Core elements:

- `ampinfer`  
  Runs micropipelines (OpChain) that perform preprocessing, inference, and postprocessing.
  Produces structured results into Perception.
  OpChains can contain other processing steps, but inference is the most important one in this project.

- `ampsink`  
  Provides WebRTC-based output to a browser for stable, low-latency A/V visualization from containerized pipelines.

- `amposd`  
  Visualizes results by decorating video frames using Perception metadata.
  Planned extension: DMABUF-based Vulkan rendering for zero-copy pipelines.

- `amptracker`  
  Tracks detections across frames and stabilizes identities and trajectories over time.

- `ampperformance`  
  Collects and exposes performance metrics to support profiling and runtime analysis.

Each element can be placed into any existing GStreamer pipeline as a modular building block.

---

## Op System and micropipelines

The internal processing model is based on an Op system.

- An `Op` is a modular processing unit with a strict lifecycle (`configure`, `bind`, `process`).
- Ops are composed into ordered pipelines called `OpChain`.
- OpChains define “micropipelines” that implement a specific processing goal
  (e.g., face detection, gaze estimation, OCR detection, classification).

OpChains are created from JSON descriptors and can be executed inside `ampinfer`
or as standalone pipelines without GStreamer.

This enables pipeline composition and model swapping without recompilation.
The micropipelines are flexible enough to define non-inference tasks.
Different inference engines can be used even within a single micropipeline.

---

## Configuration Model

All runtime setup is currently defined in JSON.

JSON configuration is used to describe:

- Which elements build up the micropipeline (OpChain)
- Op attributes (models, thresholds, parsers, preprocessing settings)
- Backend selection also configured via Ops
- Model cascading

---

## Perception Data Model

Perception is the persistent inference result container.

- It travels downstream alongside the media buffer.
- It aggregates structured results across pipeline stages.
- Each inference or postprocessing stage can append new Perception Layers.

A Perception Layer captures one inference step and its outputs.
This enables complex pipelines such as:

- multi-model cascades (detection → refinement → classification)
- parallel branches (audio + video inference)
- incremental enrichment across elements

- [Perception](perception.md)  
  See details here.


---

## Tracking

`amptracker` uses Perception detections and derives cross-frame information from them.
In the ideal case, it creates an **entity** that represents a real-world object.
In that case, a missing detection on a single frame does not mean that the entity is lost.

Goals:

- Improve temporal stability of detections.
- Preserve identities across time.
- Attach track identifiers and trajectories back into Perception.

Today, it updates tracked detections in-place and can emit `TrackTrace` layers.
Richer tracker-specific output layers are still evolving.

---

## Visualization and Rendering

`amposd` uses Perception to render visual overlays on video frames.

Current behavior:

- Decorates frames with inference results (rectangles, labels, etc.).

Planned behavior:

- DMABUF-based Vulkan rendering to support zero-copy pipelines.
- Reduced latency and CPU overhead for high-performance edge deployments.

---

## End-to-End Data Flow

A typical execution flow:

1. GStreamer delivers an audio/video buffer into the pipeline.
2. `ampinfer` executes an OpChain for preprocessing → inference → postprocessing.
3. Results are written into Perception as one or more Layers.
4. `amptracker` optionally stabilizes detections across frames and can append `trackTrace` output.
5. `amposd` optionally visualizes Perception results on video frames.
6. `ampsink` optionally streams the output to a browser via WebRTC.
7. `ampperformance` records runtime performance information.

This modular architecture enables flexible composition while keeping the core inference engine reusable outside GStreamer.
