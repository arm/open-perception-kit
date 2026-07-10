# yolo-benchmark

Manual/nightly benchmark comparing a bare Ultralytics YOLO predict loop with
the PEK OpChain runtime on the same prepared COCO image list.
This is not a PR gate. PR runs require the `run-yolo-benchmark` label; manual
and nightly runs repeat the benchmark 10 times by default.

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

```sh
YOLO_BENCHMARK_RUNS=10 ./examples/yolo-benchmark/docker/run.sh benchmark
```

Artifacts are under `artifacts/yolo-benchmark`:

```text
images.tsv
runs/run-XX/{bare,pek}/{benchmark_summary.json,predictions.jsonl,timings.jsonl}
runs/run-XX/{comparison.json,comparison.md}
```
## Pages

The Pages publisher consumes the benchmark Actions artifact and publishes under
`yolo-benchmark/` plus the deploy-only `yolo-performance-datasets/` overlay.

Dataset images are restored as a deploy-only overlay keyed by fingerprint.

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
UPSTREAM_RUN_ATTEMPT=local \
UPSTREAM_RUN_ID=local \
./examples/yolo-benchmark/pages/run.sh publish
```
