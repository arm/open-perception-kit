# yolo-benchmark

`yolo-benchmark` is a manual benchmark example for comparing a bare Ultralytics
YOLO predict loop with the PEK OpChain runtime on the same image list.

This is not a PR gate. It is intended for manual and nightly runs where the
artifact is reviewed as a performance regression signal.

## Measurement Cut

The benchmark cut is defined by `schema/benchmark_summary.schema.json`.

Both runners load the model separately, preload all input images before the
timed loop, run one warmup pass, then measure one in-process image loop. JPEG
decode, image file I/O, and camera/color adapter cost are outside the per-image
timing. JSONL artifact writing is also outside each per-image timer.

The common summary artifact is:

```text
benchmark_summary.json
```

Each runner also writes `timings.jsonl` next to `predictions.jsonl`. It contains
one row per image with the dataset index, image id/path, dimensions, detection
count, measured per-image wall time, and `preprocess_ms`, `inference_ms`, and
`postprocess_ms` components. Bare uses the Ultralytics per-result speed fields;
PEK uses the existing OpChain performance trace scopes around preprocess,
inference, and postprocess ops. This is the source for tail-latency analysis
such as "which dataset quarter produced the fastest or slowest images".

The schema is documented in `schema/benchmark_summary.schema.json`. The compare
script also checks the shared measurement block before it writes a delta report.

## Layout

```text
bare/benchmark.py  bare Ultralytics sample runner
bare/requirements.txt
pek/benchmark.cpp  PEK OpChain sample runner
tests/             unit tests for the Python helpers
```

## Build PEK Runner

Prepare the Docker benchmark environment:

```sh
./examples/yolo-benchmark/docker/run.sh setup
```

The build installs:

```text
examples/bin/yolo-benchmark
```

## Dataset

The input list is a TSV file:

```text
# image_set_fingerprint=sha256:<dataset-and-limit-fingerprint>
image_id<TAB>/absolute/path/to/image.jpg
```

Generate it with `prepare_dataset.py`; the runners require the fingerprint
header so a comparison cannot accidentally mix different image sets.

The preferred Docker flow prepares `IMAGE_LIST` during `docker/run.sh setup`.

For CI or smoke runs, prepare a limited image list explicitly:

```sh
python3 examples/yolo-benchmark/prepare_dataset.py \
  --coco-dir datasets/coco \
  --output artifacts/yolo-benchmark/images.tsv \
  --limit 500
```

If the dataset is already present, the helper only writes the image list. If it
is missing, it downloads the official COCO val2017 images and annotations first,
verifies their SHA-256 hashes, CRC-checks the ZIPs, and extracts only safe paths.
The helper accepts both common layouts:

```text
datasets/coco/images/val2017
datasets/coco/val2017
```

To download the official COCO val2017 images and annotations without pulling the
train split:

```text
https://s3.amazonaws.com/images.cocodataset.org/zips/val2017.zip
https://s3.amazonaws.com/images.cocodataset.org/annotations/annotations_trainval2017.zip
```

Use `--limit` on `prepare_dataset.py` for quick smoke runs. Headline runs
should use the full official COCO val2017 split.

## Run

Preferred host entry point:

```sh
./examples/yolo-benchmark/docker/run.sh setup
./examples/yolo-benchmark/docker/run.sh benchmark
```

This uses Docker Compose to run inside the PEK development image with the repo
mounted at `/work`, matching the checked-in OpChain paths and ONNX Runtime
runtime layout. `run.sh setup` builds the Compose runtime image, creates or reuses
the Docker cache volume for the dataset, bare runner venv, and PEK sample build,
and writes the image list. `run.sh benchmark` only runs benchmark commands and writes
the artifacts listed below.

CI runs the same Compose entry point nightly and on PRs labeled
`run-yolo-benchmark`, with setup and measurement split into separate log steps.
CI repeats the benchmark 10 times by default and writes each full report under
`artifacts/yolo-benchmark/runs/run-XX/`. Manual runs accept `image_limit` and
`benchmark_runs` inputs; `image_limit=0` means full COCO val2017.

For a quick smoke image list:

```sh
YOLO_BENCHMARK_LIMIT=500 ./examples/yolo-benchmark/docker/run.sh setup
./examples/yolo-benchmark/docker/run.sh benchmark
```

To repeat the same prepared benchmark locally:

```sh
YOLO_BENCHMARK_RUNS=10 ./examples/yolo-benchmark/docker/run.sh benchmark
```

Default inputs:

```text
MODEL=config/models/yolov11/yolo11n-fp32-320.onnx
OPCHAIN=config/models/yolov11/opchain.json
IMAGE_LIST=artifacts/yolo-benchmark/images.tsv
ARTIFACT_ROOT=artifacts/yolo-benchmark
```

The sample fixes image size to `320`, device label to `cpu`, and warmup to one
image because those are part of this benchmark definition, not runtime knobs.

Outputs:

```text
artifacts/yolo-benchmark/images.tsv
artifacts/yolo-benchmark/runs/run-XX/bare/benchmark_summary.json
artifacts/yolo-benchmark/runs/run-XX/bare/predictions.jsonl
artifacts/yolo-benchmark/runs/run-XX/bare/timings.jsonl
artifacts/yolo-benchmark/runs/run-XX/pek/benchmark_summary.json
artifacts/yolo-benchmark/runs/run-XX/pek/predictions.jsonl
artifacts/yolo-benchmark/runs/run-XX/pek/timings.jsonl
artifacts/yolo-benchmark/runs/run-XX/comparison.json
artifacts/yolo-benchmark/runs/run-XX/comparison.md
```

The Markdown comparison contains the main per-image table:

```text
metric | bare ms | PEK ms | delta ms | ratio | delta %
```

Keep `artifacts/` local; it is ignored by git.

## Pages report

The Pages publisher mirrors the Playwright report style without reusing the
Playwright publisher code or CSS. The benchmark job keeps producing the
`yolo-benchmark-<run-id>-<attempt>` Actions artifact, and the `Publish YOLO
Benchmark Reports` workflow consumes that artifact after the run completes.

Published reports live under the shared repository Pages site:

```text
yolo-benchmark/nightly/
yolo-benchmark/manual/<run-id>/
yolo-benchmark/prs/<number>/
```

The report page renders native SVG percentile plots for `p50_ms`, `p75_ms`,
`p95_ms`, and `p99_ms`. The overall summary badge uses `avg_ms`. The publisher reads
per-image timing JSONL files to render the run-level breakdown table, but raw
predictions and timing JSONL files stay in the Actions artifact; Pages keeps
only the rendered table plus comparison and summary JSON files.

Local dry-run publish from an existing artifact directory:

```sh
YOLO_PAGES_DRY_RUN=1 \
YOLO_PAGES_LOCAL_ARTIFACT_DIR=artifacts/yolo-benchmark \
YOLO_PAGES_SITE_DIR=tmp/yolo-pages-local \
GITHUB_REPOSITORY=Arm-Debug/amp-dev-forge \
UPSTREAM_CONCLUSION=success \
UPSTREAM_EVENT=workflow_dispatch \
UPSTREAM_HEAD_BRANCH="$(git branch --show-current)" \
UPSTREAM_HEAD_REPOSITORY=Arm-Debug/amp-dev-forge \
UPSTREAM_HEAD_SHA="$(git rev-parse HEAD)" \
UPSTREAM_RUN_ATTEMPT=local \
UPSTREAM_RUN_ID=local \
./examples/yolo-benchmark/pages/run.sh publish
```
