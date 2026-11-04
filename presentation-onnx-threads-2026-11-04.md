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

**Overview**

ONNX Runtime has two ways to divide CPU work.
 - IntraOp splits one operator, such as a convolution, or a dotproduct etc. across threads.
 - InterOp runs independent operators in the same model graph concurrently.
Operators still wait for their inputs, and neither setting runs separate video frames in parallel.

Originally, OPK chose the inference IntraOp thread count from the hardware and fixed the InterOp thread count as 1.
This two PR has made the thread counts configurable per inference Op.

More threads are not always faster. They add synchronization and compete with decoding, preprocessing and result handling. Some models benefit from two threads within each operator and two threads between independent operators. Others work better with three IntraOp threads. The performance is also depending on the capabilities of the hardware the software is executed on. The optimal thread counts were different on my laptop than on the RaspberryPi

Configuration fragment for `opk-onnx-ops/Inference` attributes:

```json
{
  "intraOpNumThreads": 2,
  "interOpNumThreads": 2
}
```

**Results**

Together, the IntraOp and InterOp changes improve nano-model pipeline throughput by 5.0 to 7.3 percent over the original baseline. Small-640 gains 3.9 percent. Small-320 is unchanged, while small-480 is 0.3 percent lower. A further IntraOp sweep with InterOp held fixed found no repeatable improvement over the current settings.

**Slide table — combined IntraOp + InterOp change vs baseline**

Percentages compare the medians of three real-video runs from the chronological benchmark.

| Model | IntraOp threads | InterOp threads | Before FPS | After FPS | Combined change vs baseline |
| --- | ---: | ---: | ---: | ---: | ---: |
| yolo26n-320 | 2 | 2 | 18.05 | 19.28 | +6.8% |
| yolo26n-480 | 2 | 2 | 9.58 | 10.06 | +5.0% |
| yolo26n-640 | 2 | 2 | 5.53 | 5.93 | +7.3% |
| yolo26s-320 | 3 | 1 | 10.04 | 10.04 | 0.0% |
| yolo26s-480 | 3 | 1 | 4.90 | 4.90 | 0.0% |
| yolo26s-640 | 3 | 2 | 2.70 | 2.80 | +3.9% |

Before FPS is the original baseline; after FPS includes both the IntraOp and InterOp changes. Both are medians of three complete-video runs. Thread counts are for the combined stage; the baseline used IntraOp 3 and InterOp 1.

The benchmark was created with the GettyImages-1140581459.mov video. Every run processed all of the frames, and the first 30 of these were not counted in the metrics (they tend to have highly different numbers, so this period was considered as a warmup). 
