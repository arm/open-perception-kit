# Changelog

All notable changes to this project will be documented in this file.

## [0.1.1-alpha.1] - 2026-03-18

### Minimal tracking, Topo support and container fixes

* Add topo support with runtime arg by @gergelybado in https://github.com/Arm-Debug/amp-dev-forge/pull/44
* Add minimal tracker implementation by @zmolnar in https://github.com/Arm-Debug/amp-dev-forge/pull/39
* Container fixes and Classification Hailo version by @gergelybado in https://github.com/Arm-Debug/amp-dev-forge/pull/47

## [0.1.0-alpha.1] - 2026-03-04
### Highlights

- Core GStreamer elements:
  - ampinfer: inference element wired to the AMP OpChain runtime (ONNX / Hailo backends via ops-onnx, ops-hailort).
  - ampperformance: performace measurement element, collects performance info of the different code paths.
  - amposd / ampsink: elements for on-screen decoration and information display and sink integration (WebRTC/HTTP control when the optional web server stack is available).
  - Meson-based build and test setup under [development/](development).
  - Convenience scripts for building elements and running example pipelines (see [scripts/](scripts)).
  - Containerized dev environment suitable for use via DevContainers/Docker.

### Documentation & Examples

- Know-how tutorials in [docs/corespec/how-to.md](docs/corespec/how-to.md)
- Entry-point docs in [README.md](README.md) and [docs/README.md](docs/README.md).
- Element- and tracer-specific guides:
  - [docs/AMPPERFORMANCE_ELEMENT.md](docs/AMPPERFORMANCE_ELEMENT.md)
  - [docs/PERFORMANCE_TRACER.md](docs/PERFORMANCE_TRACER.md)
- Example models, opchains, and media assets in [etc/](etc) for trying out end-to-end pipelines.

### Known Limitations / Next Steps

- APIs and configuration surfaces are still subject to change before the 0.1.0 release.
- Documentation for all elements (e.g. full ampinfer guide and pipeline recipes) is not yet complete.
- Target usage is currently via the provided container/dev environment; host-only setups are not officially supported.
- Developer documentation to make it easier to integrate new runtimes/models is still missing.
 