# yolo-benchmark

Independent manual/nightly benchmarks compare bare Ultralytics YOLO with the
PEK runtime. Neither is a PR gate:

- `YOLO Video Benchmark` uses one pinned MP4. PR runs require the
  `run-yolo-benchmark` label.
- `YOLO Imageset Benchmark` uses COCO val2017. PR runs require the
  `run-yolo-imageset-benchmark` label.

Manual video runs default to four order-balanced repetitions and scheduled
runs use ten. Scheduled image-set runs use one full-COCO repetition; labeled
PR runs use ten images as a smoke test.

## Video FPS

Video mode processes the original MP4 directly in both runners:

- Bare: `YOLO.predict(source=video, stream=True, batch=1)`
- PEK: `filesrc ! decodebin ! videoconvert ! pekinfer ! fakesink sync=false`

The first completed frame is warmup. `pipeline_fps` measures the remaining 204
serialized-result intervals and includes decode, color conversion, inference,
post-processing, result serialization, and delivery. Artifact writing is
outside the timed region. Playback is unpaced, so the source's 30 FPS
timestamps do not cap the measured throughput.

After timing completes, video mode runs separate Bare and PEK visualization
passes. Ultralytics renders `bare-detections.mp4`; `pekinfer ! pekosd` renders
`pek-detections.mp4`. Rendering and H.264 encoding are not part of the reported
FPS.

The input is MediaPipe's object-detection `test_video.mp4`, Copyright 2019 The
MediaPipe Authors, licensed under Apache-2.0. The helper downloads it from the
[MediaPipe repository at pinned revision
`0ad5a71bcdff3d756dc5b07f93765aaeb4152538`](https://github.com/google-ai-edge/mediapipe/blob/0ad5a71bcdff3d756dc5b07f93765aaeb4152538/mediapipe/examples/desktop/object_detection/test_video.mp4)
and verifies SHA-256
`710831289c00251c86eafb0b0d11cf6190fda02f95bcfaa5037511eabb5c9de8`.
The fixed input is 1920x1080, 30 FPS, and 205 frames. The MP4 is cached but not
included in benchmark artifacts.

## Measurement

Both runners load separately, preload all images, run one warmup image, then
measure `preloaded_image_to_postprocess_result_ready`. Per-image timing excludes
model load, file I/O, JPEG decode, preload, camera/color adapter cost, and JSONL
writing.

Artifacts are shape-compatible between runners: `benchmark_summary.json`,
`predictions.jsonl`, and `timings.jsonl`. Timing rows include image id/path,
dimensions, detection count, wall time, and `preprocess_ms`, `inference_ms`,
`postprocess_ms`.

## Dataset

The dataset helper downloads only the official COCO val2017 split and
annotations, verifies SHA-256/ZIP CRC, then writes:

```text
# image_set_fingerprint=sha256:<dataset-and-limit-fingerprint>
image_id<TAB>/absolute/path/to/image.jpg
```

```text
https://s3.amazonaws.com/images.cocodataset.org/zips/val2017.zip
https://s3.amazonaws.com/images.cocodataset.org/annotations/annotations_trainval2017.zip
```
Use `YOLO_BENCHMARK_LIMIT` for smoke runs. `0` means full COCO val2017.

## Docker Run

```sh
./examples/yolo-benchmark/docker/run.sh setup
./examples/yolo-benchmark/docker/run.sh benchmark
```
`setup` prepares the image, dataset, venv, and PEK sample build. `benchmark`
runs the prepared bare and PEK runners.

For the video-only path:

```sh
YOLO_BENCHMARK_KIND=video ./examples/yolo-benchmark/docker/run.sh setup
YOLO_BENCHMARK_KIND=video YOLO_BENCHMARK_RUNS=4 \
  ./examples/yolo-benchmark/docker/run.sh benchmark
YOLO_BENCHMARK_KIND=video YOLO_BENCHMARK_RUNS=4 \
  ./examples/yolo-benchmark/docker/run.sh summary
```

```sh
YOLO_BENCHMARK_RUNS=10 ./examples/yolo-benchmark/docker/run.sh benchmark
```

Image-mode artifacts are under `artifacts/yolo-benchmark`:

```text
images.tsv
runs/run-XX/{bare,pek}/{benchmark_summary.json,predictions.jsonl,timings.jsonl}
runs/run-XX/{comparison.json,comparison.md}
```

Video mode writes `video-source.json`, per-run Bare/PEK summaries and
comparisons, plus top-level `summary.json` and `summary.md` containing median
FPS. Top-level `bare-detections.mp4` and `pek-detections.mp4` files contain the
corresponding detection overlays.

## Pages

The shared Pages publisher branches on the comparison schema and publishes the
video and image-set reports independently under `yolo-benchmark/` and
`yolo-imageset-benchmark/`. Inputs are exposed through the deploy-only
`yolo-performance-datasets/` overlay.

Dataset images and the pinned input video are restored as a deploy-only overlay keyed by fingerprint.
Detection MP4s stay in the benchmark Actions artifact. Every report Pages
deployment restores only the latest successful video run as an embedded overlay;
older video reports link to their workflow run instead.

```sh
YOLO_PAGES_DRY_RUN=1 \
YOLO_PAGES_LOCAL_ARTIFACT_DIR=artifacts/yolo-benchmark \
YOLO_PAGES_SITE_DIR=tmp/yolo-pages-local \
GITHUB_REPOSITORY=Arm-Debug/amp-dev-forge \
UPSTREAM_CONCLUSION=success \
UPSTREAM_EVENT=workflow_dispatch \
UPSTREAM_HEAD_BRANCH=local \
UPSTREAM_HEAD_SHA=local \
UPSTREAM_HEAD_REPOSITORY=Arm-Debug/amp-dev-forge \
UPSTREAM_RUN_ATTEMPT=1 \
UPSTREAM_RUN_ID=1 \
./examples/yolo-benchmark/pages/run.sh publish
```
