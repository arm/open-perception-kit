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

ONNX Runtime has two ways to divide CPU work. IntraOp splits one operator, such as a convolution, across threads. InterOp runs independent operators in the same model graph concurrently. Operators still wait for their inputs, and neither setting runs separate video frames in parallel.

Originally, OPK chose the inference thread count from the hardware. On a four-core Raspberry Pi, that meant three IntraOp threads and sequential graph execution. The IntraOp change made the count configurable per inference Op. The later InterOp change exposed parallel graph execution and retuned the model presets.

More threads are not always faster. They add synchronization and compete with decoding, preprocessing and result handling. Some models benefit from two threads within each operator and two threads between independent operators. Others work better with three IntraOp threads and a sequential graph.

We measured the changes in their actual order: baseline, IntraOp, then InterOp. All six YOLO nano and small configurations ran on lecso, entirely on the CPU, using the same ONNX Runtime version and real video. Each stage was repeated three times, and every frame's detections were checked.

**Results — final 30 seconds**

The InterOp stage improves nano-model pipeline throughput by 8.3 to 10.6 percent over the IntraOp stage. The small-320 control gains 6.5 percent by changing IntraOp from two threads to three. Small-480 stays essentially unchanged. These are step-by-step gains: compared with the original baseline, nano gains 5.0 to 7.3 percent, while small-320 returns to roughly its original speed.

**Slide table — medians of three real-video runs**

| Model | Baseline FPS | + IntraOp FPS | + InterOp FPS | IntraOp step | InterOp step | Total vs baseline |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| yolo26n-320 | 18.05 | 17.80 | 19.28 | -1.4% | +8.3% | +6.8% |
| yolo26n-480 | 9.58 | 9.10 | 10.06 | -5.0% | +10.6% | +5.0% |
| yolo26n-640 | 5.53 | 5.38 | 5.93 | -2.6% | +10.2% | +7.3% |
| yolo26s-320 | 10.04 | 9.41 | 10.04 | -6.3% | +6.7% | 0.0% |
| yolo26s-480 | 4.92 | 4.91 | 4.90 | -0.2% | -0.1% | -0.3% |
| yolo26s-640 | 2.70 | 2.69 | 2.80 | -0.1% | +4.0% | +3.9% |

Every run processes all 417 frames, with 30 warm-up frames. All six files are optimized INT8 YOLO26 models, using ONNX Runtime 1.31.1 throughout. The source stages are actual cherry-picked merges. CPU preprocessing, inference, tracking, serialization and output scaling are included; encoding and browser/network delivery are excluded.

The small-320 change is an IntraOp preset adjustment included in the later InterOp merge; its graph execution remains sequential. The detailed report includes a same-source two-versus-three-thread control and explains differences from ticket EXPKITS-1360's 8% result.

Configuration fragment for `opk-onnx-ops/Inference` attributes:

```json
{
  "intraOpNumThreads": 2,
  "interOpNumThreads": 2
}
```

[Full chronological benchmark, settings and validation](experiments/onnx-threads-stages-20261103/README.md) · [ONNX Runtime threading reference](https://onnxruntime.ai/docs/performance/tune-performance/threading.html)
