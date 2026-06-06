---
sidebar_position: 8
sidebar_label: App Output
---

# Feed Inference Into An Application

```text
+--------------------------------------------------------------+
| Coming soon                                                  |
|                                                              |
| This guide will show how to consume PEK inference results    |
| from an application instead of only viewing overlays in the   |
| browser.                                                     |
+--------------------------------------------------------------+
```

This page is intentionally reserved for the application-output workflow. The
implementation guide still needs to be written and verified against the current
runtime and `peksink` control/API surface.

## What This Guide Will Cover

| Topic | Planned outcome |
| --- | --- |
| Result shape | Understand the structured inference payload emitted by PEK. |
| Transport | Choose the right output path for app integration. |
| Client code | Read detections, classifications, tracking IDs, and performance data. |
| Reliability | Handle reconnects, empty frames, disabled models, and version changes. |

## Use These Guides Today

Until this page is implemented, start with the working runtime paths:

- [Use Your Own Media](media-input.md) to keep a known pipeline and change input.
- [Use A Camera](camera-input.md) to run live camera inference.
- [Runtime Basics](runtime-basics.md) to understand the pipeline and output
  elements.
- [Structural Basics](structural-basics.md) to understand where configuration
  and runtime code live.

## Status

The target content is **to be implemented**. The page exists so links from the
quick-start flow resolve cleanly while the application-output guide is prepared.

[Back to README](index.md)
