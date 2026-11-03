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

# ONNX thread configuration: IntraOp and InterOp

Wednesday, 4 November 2026. About 90 seconds of explanation and 30 seconds for results.

**Overview — approximately 90 seconds**

ONNX Runtime offers two ways to divide CPU work. IntraOp splits one operator, such as a convolution, across several threads. InterOp runs independent operators in the same model graph at the same time. An operator must still wait for its inputs, so the graph determines how much work can overlap.

OPK exposes both settings on each inference Op. IntraOp includes the calling thread. With InterOp omitted or set to one, OPK keeps graph execution sequential. Setting it above one enables parallel graph execution; zero lets ONNX Runtime choose the pool size. These options do not make separate video frames run in parallel.

More threads can also mean more synchronization and competition for CPU time. The decoder, preprocessing and postprocessing need CPU time too, so the best inference setting must be checked in the pipeline that uses it.

We tested YOLO26 nano at 320 pixels on a Raspberry Pi 5, entirely on the CPU. Every run used the same 417-frame real video from the ONNX example. We tested all pairs from zero to four, then repeated the leading settings and the default. We checked every frame's detections and monitored temperature and throttling.

**Results — final 30 seconds**

The default reached 20.11 frames per second. Two IntraOp threads and two InterOp threads reached 21.69: a 7.9 percent gain. Mean inference time fell from 40.80 to 37.20 milliseconds. All detections matched. With two IntraOp threads and sequential execution, throughput stayed near the default while process CPU time fell. The useful setting depends on whether throughput or spare CPU capacity matters more.

**Slide table — medians of three real-video runs**

| Configuration | Pipeline FPS | Mean inference ms | Process CPU ms/frame |
| --- | ---: | ---: | ---: |
| Default: IntraOp 3, sequential | 20.11 | 40.80 | 175.03 |
| IntraOp 2, sequential | 20.26 | 40.65 | 128.81 |
| IntraOp 2, InterOp 2 | 21.69 | 37.20 | 159.97 |

Process CPU time is summed across threads, so it can exceed elapsed frame time. It is not a power measurement. The model is the optimized INT8 YOLO26n/320 file; these numbers should not be compared directly with the OPi6 FP32 GPU experiment.

Configuration fragment for the existing `opk-onnx-ops/Inference` attributes:

```json
{
  "intraOpNumThreads": 2,
  "interOpNumThreads": 2
}
```

[Full grid, methods, validation and saved evidence](experiments/onnx-threads-20261103/README.md) · [ONNX Runtime threading reference](https://onnxruntime.ai/docs/performance/tune-performance/threading.html)
