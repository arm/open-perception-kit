---
sidebar_position: 7
sidebar_label: Known limitations
---

# Known Limitations

This page captures the most important current limitations and architectural friction points in AMP Development Forge.

The goal of this page is not to block experimentation. The goal is to make the current boundaries explicit, so model integration work, postprocessing work, and application work can be planned with realistic expectations.

## Why this page exists

The current system already provides a useful end-to-end path:

- models can be described with `model.json`
- inference chains can be composed with `opchain.json`
- structured results can be written into `Perception`
- `amposd` can render several built-in result types
- `ampsink` can expose a browser-facing runtime stack

That said, some important parts of the current system are still tightly coupled, harder to extend than they should be, or broader in scope than they should be in the long term.

## Current friction points

### Postprocessing is still C++-heavy

Today, custom postprocessing is centered around C++ parser implementations under `development/ops-std/postproc/`.

That works, but it raises the cost of experimentation:

- simple output interpretation changes still require C++ source edits
- parser iteration requires rebuilds
- model specialists who are comfortable with Python cannot stay in a lighter-weight workflow
- rapid trial-and-error for output decoding is slower than it should be

This is one of the biggest usability gaps in the current extension story.

#### Proposed solution

A possible direction is to add Python postprocessing support.

That would allow users to:

- implement output parsing more quickly in Python
- iterate on postprocessing without a full C++ rebuild cycle
- keep model-integration experimentation in a more accessible workflow

The C++ parser path would still remain valuable for productionized or performance-sensitive implementations, but Python postprocessing would significantly lower the entry cost.

### Perception structures are code-defined, not contract-defined

At the moment, `Perception` structures are defined directly in C++.

That gives the runtime a clear internal type model, but it also means the result contract is owned by source code instead of by a more explicit schema layer.

In practice, this makes it harder to:

- define result contracts independently from runtime implementation
- share result definitions cleanly across repositories or products
- evolve result formats through a schema-first process
- align application-layer consumption with a stable contract artifact

#### Proposed solution

A better long-term direction would be to define `Perception` structures from explicit contracts such as JSON or YAML schemas and let runtime code follow those contracts.

### Visualization is too closely tied to runtime implementation

`amposd` currently knows how to draw specific result types directly in C++.

That is useful for debugging, but it couples visualization behavior too tightly to the runtime implementation.

In the long term, the drawing layer should be more generic:

- `Perception` should describe the result
- a JSON or YAML visualization mapping should describe how that result is rendered
- `amposd` should draw according to that mapping rather than embedding model-specific drawing rules directly in C++

That would reduce the need to modify the overlay code every time a new result type is introduced.

#### Proposed solution

The long-term direction should be a more generic perception-to-visualization mapping layer.

That could work like this:

- `Perception` continues to describe only the structured result
- a JSON or YAML visualization contract describes how each result should be drawn
- `amposd` uses that contract instead of hardcoded model-specific drawing branches

An additional step beyond `amposd` could be a browser-side drawing solution. In that setup, the structure would only describe the correspondence between `Perception` content and visualization elements, and the actual drawing could happen in a browser or application layer instead of only inside the runtime overlay.

### Application concerns are mixed into the repository scope

The repository currently contains not only inference/runtime pieces, but also browser-facing and application-facing delivery pieces.

In particular, `ampsink` includes:

- WebRTC delivery
- HTTP serving
- WebSocket control
- static web content integration

This makes the repository useful for demos and integrated development, but it also expands its scope beyond the core perception/runtime problem.

#### Proposed solution

A cleaner architectural split would keep frame/result gathering and contract-based result publication close to the perception runtime, while moving visualization hosting and application-layer UI concerns into a higher-level application stack.
One possible follow-on is a higher-level Cairn SDK binding on top of the perception results, but that is not part of the current repository.

### Camera and image acquisition is not yet uniform

Today, camera handling is still troublesome because camera sources are not uniform across platforms and real devices.

In practice, this means:

- source elements differ between environments
- camera formats and caps can differ significantly
- device-specific setup is often needed before a pipeline works
- image acquisition logic is harder to treat as a portable, stable layer than it should be

This is manageable for demos and guided setups, but it is a limitation for real-life camera-driven scenarios.

#### Proposed solution

Later on, the intended direction is to rely on the Cairn SDK abstraction layer for image acquisition in real camera scenarios.

That would make it easier to:

- request the images the application actually needs
- hide device-specific camera details behind a more uniform interface
- reduce platform-specific source handling inside AMP pipeline examples
- keep camera acquisition concerns at the right abstraction level for applications

## Conclusion

Until these limitations are addressed, it is safest to treat the system like this:

- `model.json` and `opchain.json` are the main supported integration interfaces
- `Perception` is the main structured runtime result format
- `amposd` is primarily a debugging and inspection overlay
- the browser/UI stack is a convenient demo path, not the final architectural boundary for product applications
