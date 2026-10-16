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

# Runtime benchmark

Measures an OPK element chain with fixed input frames.

Run inside the Linux development container:

```sh
./scripts/build.sh release true
meson compile -C development/build-active runtime-benchmark
build="$(realpath development/build-active)/meson-out"
export GST_PLUGIN_PATH="$build${GST_PLUGIN_PATH:+:$GST_PLUGIN_PATH}"
export LD_LIBRARY_PATH="$build${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

"$build/runtime-benchmark" \
  --dma-heap /dev/dma_heap/system --frames 10000 --warmup 1000 \
  --pipeline 'opktracker ! opkperformance enabled=true update-interval=1 ! opkosd enabled=false'
```

Use `--shared-input` to retain an input reference, as another tee consumer might.
Omit `--dma-heap` for system memory. A requested DMA heap must work; there is no
simulated DMA or copy fallback. `--width`, `--height` and `--format` select the
input layout; defaults are NV12 640×480.

For inference, prepend `opkinfer opchain-path=/work/config/models/yolo26n-320/opchain.json`
to the chain. The model must already be downloaded by `opk-models`. Compare the
default against `copy-input=true` on that first `opkinfer`. Shared DMA input
with OSD drawing requires `copy-input=true` on `opkosd`.

The final stdout line is JSON: mean, median and p95 processing time, process CPU
time per frame (all threads), and counts of output frames retaining the input
`GstMemory`, DMA memory and results metadata. Run `identity` to measure harness
overhead. Every input must produce one synchronous output with the same PTS;
queues, sources, sinks and frame-dropping chains are unsuitable.

Timing covers GStreamer's push/pull through the chain. It excludes startup,
warmup, input construction, output checks and buffer teardown. DMA input cycles
through four preallocated allocations, wrapped in fresh `GstMemory` objects per
frame. Pixels are reset to gray outside timing, including after drawing. This
warms the CPU cache and mapping; it does not model camera cache behavior.
`processing_fps` is unpaced processing capacity, not camera or display FPS.
Capture, encoding, browser synchronization and display
are not measured.

Use identical release settings and benchmark source for both revisions. Alternate
their execution order, repeat each case, and compare run medians. Record the
commits, compiler, GStreamer version, CPU governor and throttling state with the
results; do not compare a debug build with a release build.

## Pi 5 results, 2026-10-16

Compared `b0dbdb4a` (DMA input support, before forwarding changes) with `cefb281f`.
Both used GCC 14.2, GStreamer 1.26.2, release `-O3`/LTO, the `performance` CPU
governor at 2.4 GHz, and `/dev/dma_heap/system`. No throttling occurred.
Values are medians of six run means; execution order was balanced. Input is warm,
gray NV12. [Individual runs and CPU times](rpi5-2026-10-16.csv).

| Chain / input | Before, ms/frame | Current, ms/frame |
| --- | ---: | ---: |
| Metadata, 640×480, unique | 0.0251 | 0.0072 |
| Metadata, 640×480, shared | 0.0720 | 0.0079 |
| Metadata, 1920×1080, unique | 0.0462 | 0.0066 |
| Metadata, 1920×1080, shared | 0.5378 | 0.0075 |
| Drawing, 640×480, unique | 0.0529 | 0.0429 |
| Drawing, 1920×1080, unique | 0.0825 | 0.0637 |
| YOLO26n-320, 640×480, unique | 33.467 | 33.511 |
| YOLO26n-320, 640×480, shared | 33.631 | 33.564 |

Metadata uses the command above; drawing enables `opkosd`; YOLO prepends
`opkinfer` with drawing disabled. The `identity` control took 0.0028 ms in both
builds. Each metadata/control run measured 10,000 frames after 1,000 warmup
frames; drawing used 3,000/300 and YOLO used 200/50.

The current direct mode retained the original DMA memory in every measured
frame. The previous implementation copied shared inputs into CPU memory.
Forwarding is faster; YOLO's run ranges overlap, so there is no demonstrated
inference speedup. Explicit `copy-input=true` also took about 33.6 ms with YOLO.
