---
title: Feed Inference Into An Application
sidebar_position: 6
sidebar_label: App Output
description: Placeholder for the planned workflow to consume Open Perception Kit inference results from an application.
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


# Feed Inference Into An Application

```text
+--------------------------------------------------------------+
| Coming soon                                                  |
|                                                              |
| This guide will show how to consume OPK inference results    |
| from an application instead of only viewing overlays in the   |
| browser.                                                     |
+--------------------------------------------------------------+
```

This page is intentionally reserved for the application-output workflow. The
implementation guide still needs to be written and verified against the current
runtime and `opksink` control/API surface.

## What This Guide Will Cover

| Topic | Planned outcome |
| --- | --- |
| Result shape | Understand the structured inference payload emitted by OPK. |
| Transport | Choose the right output path for app integration. |
| Client code | Read detections, classifications, tracking IDs, and performance data. |
| Reliability | Handle reconnects, empty frames, disabled models, and version changes. |

## Use These Guides Today

Until this page is implemented, start with the working runtime paths:

- [Use Your Own Media](media-input.md) to keep a known pipeline and change input.
- [Use A Camera](camera-input.md) to run live camera inference.
- [Runtime Basics](../concepts/runtime-basics.md) to understand the pipeline and output
  elements.
- [Structural Basics](../concepts/structural-basics.md) to understand where configuration
  and runtime code live.

## Status

The target content is **to be implemented**. The page exists so links from the
quick-start flow resolve cleanly while the application-output guide is prepared.

[Back to How-To Guides](/how-to)
