# Changelog

All notable changes to this project will be documented in this file.

## [0.3.0] - 2026-09-07

### Perception SDK distribution

- Add the generated Rust Perception SDK with typed FrameResults packet APIs and cross-language tests, and publish its versioned crate to the internal Cargo registry as part of the stable release flow ([#384](https://github.com/Arm-Debug/amp-dev-forge/pull/384), [#410](https://github.com/Arm-Debug/amp-dev-forge/pull/410)).
- Add producer identity metadata across the generated SDKs and update the standalone C++ examples to consume schema-based runtime packets ([#362](https://github.com/Arm-Debug/amp-dev-forge/pull/362), [#399](https://github.com/Arm-Debug/amp-dev-forge/pull/399)).

### Runtime and inference

- Add stateful embedded Python postprocessing with read-only zero-copy tensor views, generated Perception APIs, and a MobileNet classification example ([#362](https://github.com/Arm-Debug/amp-dev-forge/pull/362)).
- Add an isolated BlazeFace bring-your-own-model example covering model setup, Python postprocessing, external result transport, and optional visualization ([#414](https://github.com/Arm-Debug/amp-dev-forge/pull/414)).
- Automatically enable transitive upstream model dependencies and show those dependencies in DebugUI ([#380](https://github.com/Arm-Debug/amp-dev-forge/pull/380), [#385](https://github.com/Arm-Debug/amp-dev-forge/pull/385)).
- Consolidate C++ performance tracing in `PerformanceMetrics` while preserving overlays, runtime snapshots, CSV export, and benchmark timing ([#409](https://github.com/Arm-Debug/amp-dev-forge/pull/409)).
- Fix metadata-copy null handling and the lifetime of embedded C++ members in `pekperformance` and `pekosd` ([#406](https://github.com/Arm-Debug/amp-dev-forge/pull/406), [#404](https://github.com/Arm-Debug/amp-dev-forge/pull/404)).

### Platform and developer workflow

- Remove the unsupported Hailo and NCNN inference backends and their configuration, build, runtime, and documentation surfaces ([#382](https://github.com/Arm-Debug/amp-dev-forge/pull/382), [#400](https://github.com/Arm-Debug/amp-dev-forge/pull/400)).
- Support native checkouts outside `/work` through `PEK_PROJECT_ROOT` and stable native build paths ([#349](https://github.com/Arm-Debug/amp-dev-forge/pull/349)).
- Fix independent WebUI sidebar and output controls and make copy actions work on HTTP origins ([#388](https://github.com/Arm-Debug/amp-dev-forge/pull/388)).
- Improve CI and release reliability with scoped caches, artifact-based image handoff, remote Valgrind baselines, Black Duck lifecycle management, and non-blocking release observability ([#366](https://github.com/Arm-Debug/amp-dev-forge/pull/366), [#370](https://github.com/Arm-Debug/amp-dev-forge/pull/370), [#367](https://github.com/Arm-Debug/amp-dev-forge/pull/367), [#391](https://github.com/Arm-Debug/amp-dev-forge/pull/391), [#401](https://github.com/Arm-Debug/amp-dev-forge/pull/401), [#408](https://github.com/Arm-Debug/amp-dev-forge/pull/408), [#364](https://github.com/Arm-Debug/amp-dev-forge/pull/364)).

## [0.2.1] - 2026-08-26

### Perception SDK distribution

- Publish the release-aligned OPK Perception SDK wheel to Artifactory PyPI for locked Python consumption while retaining the native PEK package for runtime plugins and models.

## [0.2.0] - 2026-08-24

### Perception SDK and configuration

- Replace the hand-written perception result model and wire format with schema-generated FrameResults, and add generated C++, Python, and TypeScript SDKs used across the runtime, browser, publishing, and Plumber paths ([#217](https://github.com/Arm-Debug/amp-dev-forge/pull/217)).
- Define and enforce versioned v1 Model and OpChain JSON descriptor schemas, add `pek-config-check`, and integrate validation into runtime, download, build, and release flows ([#296](https://github.com/Arm-Debug/amp-dev-forge/pull/296), [#297](https://github.com/Arm-Debug/amp-dev-forge/pull/297), [#255](https://github.com/Arm-Debug/amp-dev-forge/pull/255)).
- Embed the verified Perception SDK ZIP, checksum, and provenance in Linux release packages while packaging descriptor schemas separately ([#331](https://github.com/Arm-Debug/amp-dev-forge/pull/331)).

### Runtime and inference

- Add direct I420, NV12, and YUY2 preprocessing with negotiated color metadata and RGB or grayscale tensor output ([#291](https://github.com/Arm-Debug/amp-dev-forge/pull/291)).
- Derive tracker Kalman timing from buffer running times, with configurable fallback behavior for missing or invalid timestamps ([#327](https://github.com/Arm-Debug/amp-dev-forge/pull/327)).
- Fix `pekinfer` retry ownership and ONNX Runtime setup and teardown leaks on repeated or failed starts ([#345](https://github.com/Arm-Debug/amp-dev-forge/pull/345), [#346](https://github.com/Arm-Debug/amp-dev-forge/pull/346)).
- Clarify model, task, and runtime names in the browser model selector ([#268](https://github.com/Arm-Debug/amp-dev-forge/pull/268)).

### Packaging and developer workflow

- Include the experimental ExecuTorch backend and YOLOX model in Linux binary releases ([#316](https://github.com/Arm-Debug/amp-dev-forge/pull/316)).
- Publish the runnable deployment image for both supported architectures and reuse its validated build for release archives ([#334](https://github.com/Arm-Debug/amp-dev-forge/pull/334)).
- Consolidate supported builds on `scripts/build.sh`, reuse checksum-verified Docker-owned demo media, and remove the duplicate prerequisite path ([#338](https://github.com/Arm-Debug/amp-dev-forge/pull/338), [#336](https://github.com/Arm-Debug/amp-dev-forge/pull/336), [#354](https://github.com/Arm-Debug/amp-dev-forge/pull/354)).
- Reduce repeated ExecuTorch setup and compilation time through validated cache reuse, automatic parallelism, and `ccache` support ([#318](https://github.com/Arm-Debug/amp-dev-forge/pull/318)).

## [0.1.7] - 2026-08-10

### Packaging and deployment

- Hotfix: Fix artifactory upload failure in the release-packages workflow and add publication tests to catch similar issues before release in the future.

## [0.1.6] - 2026-08-06

### Packaging and deployment

- Add validated Linux x86_64, Linux AArch64, and offline-site release archives, with automated GitHub and Artifactory publication ([#265](https://github.com/Arm-Debug/amp-dev-forge/pull/265)).
- Reorganize development and deployment containers, add AArch64 cross-compilation, and slim the runtime images ([#215](https://github.com/Arm-Debug/amp-dev-forge/pull/215), [#275](https://github.com/Arm-Debug/amp-dev-forge/pull/275)).
- Resolve pinned model artifacts from Hugging Face during builds so release packages run offline ([#257](https://github.com/Arm-Debug/amp-dev-forge/pull/257)).

### Runtime and inference

- Add C++ runtime APIs for asynchronous Pipeline and OpChain execution, foreign-backed video frames, an experimental NCNN backend, and expanded ExecuTorch deployment support ([#132](https://github.com/Arm-Debug/amp-dev-forge/pull/132), [#222](https://github.com/Arm-Debug/amp-dev-forge/pull/222)).
- Add opt-in GStreamer QoS-aware inference scheduling while preserving video flow and tracker continuity ([#286](https://github.com/Arm-Debug/amp-dev-forge/pull/286)).
- Add grayscale preprocessing.
- Add aspect-ratio-preserving letterbox resize support ([#185](https://github.com/Arm-Debug/amp-dev-forge/pull/185)).
- Replace the performance tracing path with structured metrics and CSV export, including reliable cycle-end measurement retention ([#213](https://github.com/Arm-Debug/amp-dev-forge/pull/213), [#285](https://github.com/Arm-Debug/amp-dev-forge/pull/285)).
- Introduce shared asynchronous logging with console and file output ([#186](https://github.com/Arm-Debug/amp-dev-forge/pull/186), [#220](https://github.com/Arm-Debug/amp-dev-forge/pull/220)).

### Web UI, streaming, and examples

- Expand the Web UI with supervisor and model-dependency controls, client-side overlays, ROI crop and scale controls, resizable output panels, and fullscreen metrics ([#218](https://github.com/Arm-Debug/amp-dev-forge/pull/218)).
- Improve WebRTC reliability with TURN support, deterministic reconnect handling, media startup after answer delivery, and MTU-safe streaming for VPN and WSL environments ([#101](https://github.com/Arm-Debug/amp-dev-forge/pull/101), [#146](https://github.com/Arm-Debug/amp-dev-forge/pull/146), [#193](https://github.com/Arm-Debug/amp-dev-forge/pull/193), [#288](https://github.com/Arm-Debug/amp-dev-forge/pull/288)).
- Add schema-backed perception metadata streaming over WebSocket and TCP, Hailo 10 face-recognition and tracking configurations, and crowd-count pipelines ([#161](https://github.com/Arm-Debug/amp-dev-forge/pull/161)).
- Add reproducible YOLO image and video benchmarks with per-image timing, FPS metrics, and regression reports ([#182](https://github.com/Arm-Debug/amp-dev-forge/pull/182), [#235](https://github.com/Arm-Debug/amp-dev-forge/pull/235), [#242](https://github.com/Arm-Debug/amp-dev-forge/pull/242)).

## [0.1.5-alpha.2] - 2026-05-18

### Raspberry Pi setup

- Clarify the Raspberry Pi 5 Dev Container profiles so Hailo 8 and Hailo 10 are explicitly optional ([#105](https://github.com/Arm-Debug/amp-dev-forge/pull/105)).

## [0.1.5-alpha.1] - 2026-05-18

### Models, pipelines, and UI

- Add an advanced background-replacement demo using segmentation output ([#72](https://github.com/Arm-Debug/amp-dev-forge/pull/72)).
- Add HWC tensor input support, broader tensor and parser handling, and updated person-classification configuration.
- Add full ONNX presets for Raspberry Pi and USB camera inputs.
- Apply the PEK naming across the project and refresh the browser model controls and streaming UI.

## [0.1.4-alpha.1] - 2026-04-30

### Web UI and streaming

- Rework the browser model controls, performance overlay, and model information endpoint.
- Improve WebRTC video quality and stream configuration.

## [0.1.3-alpha.1] - 2026-04-30

### CI regression checks

- Compare Valgrind results against the development baseline and consolidate the runtime test pipelines ([#76](https://github.com/Arm-Debug/amp-dev-forge/pull/76)).

## [0.1.2-alpha.1] - 2026-04-24

### Models, metadata, and validation

- Add the Camera Contact model, postprocessor, pipeline, and overlay rendering.
- Add Hailo OSNet model support for tracking pipelines.
- Introduce perception serialization, the metadata communication element, and the Plumber result-comparison tool ([#46](https://github.com/Arm-Debug/amp-dev-forge/pull/46)).
- Add Valgrind-based element checks and dedicated test pipeline presets ([#53](https://github.com/Arm-Debug/amp-dev-forge/pull/53)).
- Fix the tracker pipeline video source and add a WSL WebRTC development workaround ([#55](https://github.com/Arm-Debug/amp-dev-forge/pull/55)).

## [0.1.1-alpha.1] - 2026-03-18

### Minimal tracking, Topo support and container fixes

* Add topo support with runtime arg by @gergelybado in https://github.com/Arm-Debug/amp-dev-forge/pull/44
* Add minimal tracker implementation by @zmolnar in https://github.com/Arm-Debug/amp-dev-forge/pull/39
* Container fixes and Classification Hailo version by @gergelybado in https://github.com/Arm-Debug/amp-dev-forge/pull/47

## [0.1.0-alpha.1] - 2026-03-04
### Highlights

- Core GStreamer elements:
  - pekinfer: inference element wired to the Perception Experience Kit OpChain runtime (ONNX / Hailo backends via ops-onnx, ops-hailort).
  - pekperformance: performance measurement element, collects performance info from the different code paths.
  - pekosd / peksink: elements for on-screen decoration and information display and sink integration (WebRTC/HTTP control when the optional web server stack is available).
  - Meson-based build and test setup under [development/](development).
  - Convenience scripts for building elements and running example pipelines (see [scripts/](scripts)).
  - Containerized dev environment suitable for use via DevContainers/Docker.

### Documentation & Examples

- How-to guides in [docs/public/how-to/](docs/public/how-to/)
- Entry-point docs in [README.md](README.md) and [docs/public/index.md](docs/public/index.md)
- Architecture and element docs in [docs/public/arch/](docs/public/arch/)
- Checked-in models, opchains, and pipelines under [config/](config/)

### Known Limitations / Next Steps

- APIs and configuration surfaces are still subject to change before the 0.1.0 release.
- Documentation for all elements (e.g. full pekinfer guide and pipeline recipes) is not yet complete.
- Target usage is currently via the provided container/dev environment; host-only setups are not officially supported.
- Developer documentation to make it easier to integrate new runtimes/models is still missing.
